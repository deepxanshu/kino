#!/usr/bin/env python3
"""
kino companion -- Phase 3a: derive the list of active Codex chats + status.

READ-ONLY. Reads only ~/.codex metadata (thread ids, project cwd, timestamps,
running-process list, queued follow-ups). It never reads or prints the contents
of your conversations. Later phases stream this list over USB-serial to the
Magic Stick "Agents" page.

Usage:
    python3 codex_agents.py          # derived active-chat list
    python3 codex_agents.py --raw    # also dump the underlying signals
"""
import json
import os
import glob
import sys
import time

CODEX = os.path.expanduser("~/.codex")


def load_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except Exception:
        return None


def global_state():
    return load_json(os.path.join(CODEX, ".codex-global-state.json")) or {}


def running_conversation_ids():
    procs = load_json(os.path.join(CODEX, "process_manager", "chat_processes.json")) or []
    ids = {}
    for p in procs:
        cid = p.get("conversationId")
        if cid:
            ids.setdefault(cid, []).append(os.path.basename((p.get("cwd") or "").rstrip("/")))
    return ids


def session_file_for(thread_id):
    hits = glob.glob(os.path.join(CODEX, "sessions", "**", f"*{thread_id}*.jsonl"), recursive=True)
    return hits[0] if hits else None


def session_cwd(path):
    """Return the project cwd from a session file's meta lines (metadata only)."""
    try:
        with open(path) as f:
            for _ in range(4):
                line = f.readline()
                if not line:
                    break
                d = json.loads(line)
                payload = d.get("payload") if isinstance(d, dict) else None
                if isinstance(payload, dict):
                    for k in ("cwd", "cwd_path", "workingDirectory"):
                        if payload.get(k):
                            return payload[k]
                    git = payload.get("git")
                    if isinstance(git, dict) and git.get("repository_url"):
                        return git["repository_url"]
    except Exception:
        pass
    return None


def recent_session_files(limit):
    files = glob.glob(os.path.join(CODEX, "sessions", "**", "rollout-*.jsonl"), recursive=True)
    files.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return files[:limit]


def thread_id_from_filename(path):
    # rollout-<ISO-ts-with-dashes>-<uuid 5 groups>.jsonl
    base = os.path.basename(path)
    stem = base[len("rollout-"):-len(".jsonl")] if base.startswith("rollout-") else base
    parts = stem.split("-")
    return "-".join(parts[-5:]) if len(parts) >= 5 else stem


def derive(limit=8):
    # Option A: list = most-recently-active chats (by session-file mtime), which
    # reflects real recent activity better than thread-writable-roots (which goes
    # stale). Status: attention (queued-follow-up) > running (live process) > idle.
    g = global_state()
    active_roots = set(g.get("active-workspace-roots") or [])
    qf = g.get("queued-follow-ups")
    queued = set(qf.keys()) if isinstance(qf, dict) else set()
    running = running_conversation_ids()

    rows = []
    seen = set()
    for sf in recent_session_files(limit * 3):
        tid = thread_id_from_filename(sf)
        if tid in seen:
            continue
        seen.add(tid)
        cwd = session_cwd(sf)
        last = os.path.getmtime(sf)
        project = os.path.basename(cwd.rstrip("/")) if cwd else "?"
        if tid in queued:
            status = "attention"
        elif tid in running:
            status = "running"
        else:
            status = "idle"
        rows.append({
            "thread": tid,
            "project": project,
            "status": status,
            "last": last,
            "active": bool(cwd and cwd in active_roots),
        })
        if len(rows) >= limit:
            break
    rows.sort(key=lambda r: (r["status"] != "attention", -r["last"]))
    return rows, running, queued


def fmt_ago(epoch):
    if not epoch:
        return "?"
    s = int(time.time() - epoch)
    if s < 60:
        return f"{s}s ago"
    if s < 3600:
        return f"{s // 60}m ago"
    if s < 86400:
        return f"{s // 3600}h ago"
    return f"{s // 86400}d ago"


STATUS_CHAR = {"running": "R", "attention": "W", "error": "E", "idle": "I"}


def frame(rows, limit=7):
    """Build one '@A|name~S|name~S|...' serial line for the device."""
    parts = ["@A"]
    for r in rows[:limit]:
        short = r["thread"][-4:]
        name = f"{r['project']} {short}".replace("|", "").replace("~", "")[:AGENT_NAME_LEN - 1]
        parts.append(f"{name}~{STATUS_CHAR.get(r['status'], 'I')}")
    return "|".join(parts) + "\n"


AGENT_NAME_LEN = 20


def find_port():
    hits = glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.wchusbserial*") + glob.glob("/dev/cu.usbmodem*")
    return hits[0] if hits else None


def serial_mode(argv):
    try:
        import serial
    except ImportError:
        print("pyserial not installed. Run: pip3 install pyserial  (or use the esp-idf env python)")
        sys.exit(1)

    port = None
    for i, a in enumerate(argv):
        if a == "--serial" and i + 1 < len(argv) and not argv[i + 1].startswith("-"):
            port = argv[i + 1]
    if port is None:
        port = find_port()
    if port is None:
        print("no serial port found (plug in the stick)")
        sys.exit(1)

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.dtr = False
    ser.rts = False
    ser.open()
    try:
        ser.dtr = False
        ser.rts = False
    except Exception:
        pass
    print(f"streaming Codex chats to {port} @115200 (Ctrl-C to stop)")

    while True:
        rows, _, _ = derive()
        line = frame(rows)
        ser.write(line.encode())
        print("sent:", line.strip())
        time.sleep(1.5)


def main():
    if "--serial" in sys.argv:
        serial_mode(sys.argv)
        return
    raw = "--raw" in sys.argv
    rows, running, queued = derive()

    if raw:
        print("=== raw signals ===")
        print("running conversations:", dict(running) or "(none)")
        print("queued-follow-ups    :", queued or "(none)")
        print()

    print("=== active Codex chats ===")
    print(f"{'STATUS':<10} {'PROJECT':<14} {'THREAD':<8} LAST")
    for r in rows:
        star = "*" if r["active"] else " "
        print(f"{r['status']:<10} {r['project']:<13}{star} {r['thread'][-6:]:<8} {fmt_ago(r['last'])}")
    if not rows:
        print("(no open threads in thread-writable-roots)")


if __name__ == "__main__":
    main()
