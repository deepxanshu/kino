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


def project_name_for_path(local_projects, path):
    for _pid, p in local_projects.items():
        for rp in (p.get("rootPaths") or []):
            if rp == path:
                return p.get("name") or os.path.basename(rp.rstrip("/"))
    return os.path.basename(path.rstrip("/")) if path else "?"


def thread_title(path, fallback):
    # Title = the first *real* user message (skip <tag> boilerplate like
    # <environment_context>/<recommended_plugins>), truncated. Else fallback.
    if not path:
        return fallback
    try:
        with open(path) as f:
            for i, line in enumerate(f):
                if i > 80:
                    break
                d = json.loads(line)
                p = d.get("payload", d)
                if isinstance(p, dict) and p.get("role") == "user":
                    c = p.get("content")
                    text = None
                    if isinstance(c, str):
                        text = c
                    elif isinstance(c, list):
                        for it in c:
                            if isinstance(it, dict) and it.get("text"):
                                text = it["text"]
                                break
                    if text and not text.lstrip().startswith("<"):
                        return text.strip().split("\n")[0][:40]
    except Exception:
        pass
    return fallback


def derive(limit=8):
    # Source of truth = the Codex app's OWN open threads (thread-writable-roots),
    # with project names from local-projects. This matches what you see in the
    # app and uses the REAL conversationIds (so codex:// deep links resolve).
    g = global_state()
    local_projects = g.get("local-projects", {}) or {}
    twr = g.get("thread-writable-roots", {}) or {}
    active = set(g.get("active-workspace-roots") or [])
    qf = g.get("queued-follow-ups")
    queued = set(qf.keys()) if isinstance(qf, dict) else set()

    rows = []
    for tid, paths in twr.items():
        path = paths[0] if isinstance(paths, list) and paths else ""
        proj = project_name_for_path(local_projects, path)
        sf = session_file_for(tid)
        last = os.path.getmtime(sf) if sf else 0
        title = thread_title(sf, f"{proj} {tid[-4:]}")
        status = "attention" if tid in queued else "idle"
        rows.append({
            "thread": tid,
            "project": proj,
            "title": title,
            "status": status,
            "last": last,
            "active": path in active,
        })
    rows.sort(key=lambda r: (r["status"] != "attention", -r["last"]))
    return rows[:limit], set(), queued


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
    """Build one '@A|name~S~id|name~S~id|...' serial line for the device.
    id = full Codex conversationId, sent back by the device on select so we can
    fire `open codex://threads/<id>` to focus that thread."""
    parts = ["@A"]
    for r in rows[:limit]:
        name = r.get("title", r["project"]).replace("|", "").replace("~", "")[:AGENT_NAME_LEN - 1]
        tid = r["thread"].replace("|", "").replace("~", "")
        parts.append(f"{name}~{STATUS_CHAR.get(r['status'], 'I')}~{tid}")
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

    import threading
    import subprocess

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.3
    ser.dtr = False   # keep the ESP32 auto-reset lines deasserted so opening
    ser.rts = False   # the port doesn't reboot the stick
    ser.open()
    try:
        ser.dtr = False
        ser.rts = False
    except Exception:
        pass
    # Disable hangup-on-close (HUPCL) so quitting/killing the companion doesn't
    # toggle DTR and reset the stick -- this was the main source of reset churn.
    try:
        import termios
        attrs = termios.tcgetattr(ser.fd)
        attrs[2] &= ~termios.HUPCL
        termios.tcsetattr(ser.fd, termios.TCSANOW, attrs)
    except Exception as e:
        print("note: could not disable HUPCL:", e)
    print(f"streaming Codex chats to {port} @115200 (Ctrl-C to stop)")

    # Reader thread: the device sends "@SEL <conversationId>" when you select a
    # thread; fire the codex:// deep link to focus that thread in the Codex app.
    def reader():
        buf = b""
        while True:
            try:
                chunk = ser.read(128)
            except Exception:
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                ln, buf = buf.split(b"\n", 1)
                s = ln.decode("utf-8", errors="ignore").strip()
                if s:
                    print("recv:", s)  # debug: show all device->Mac serial lines
                if s.startswith("@SEL "):
                    tid = s[5:].strip()
                    if tid:
                        print("focus thread:", tid)
                        subprocess.run(["open", f"codex://threads/{tid}"], check=False)

    threading.Thread(target=reader, daemon=True).start()

    while True:
        rows, _, _ = derive()
        line = frame(rows)
        ser.write(line.encode())
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

    print("=== open Codex threads ===")
    print(f"{'STATUS':<10} {'TITLE':<26} {'THREAD':<8} LAST")
    for r in rows:
        star = "*" if r["active"] else " "
        print(f"{r['status']:<10} {r.get('title', '')[:24]:<25}{star} {r['thread'][-6:]:<8} {fmt_ago(r['last'])}")
    if not rows:
        print("(no open threads in thread-writable-roots)")


if __name__ == "__main__":
    main()
