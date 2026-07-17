# Kino — Magic Stick fork

A personal fork of [`xuruiray/magicstick`](https://github.com/xuruiray/magicstick) that turns an
**M5StickC Plus + MiniJoyC** into a **voice-driven coding remote**: dictate to an agent (Claude Code /
Codex) with a button, move the cursor with the joystick, cancel with the side button — without
reaching for the keyboard.

**North star:** grow this into a native, tactile controller for working with coding agents — a
physical surface to trigger dictation, steer the cursor, and (later) visualize/act on agent state.
This file is the running log so nothing gets lost as it grows.

---

## Base

- Upstream: `xuruiray/magicstick` (ESP-IDF **5.4.2**, target `esp32`, board M5StickC Plus / ESP32-PICO-D4)
- Hardware: M5StickC Plus + **MiniJoyC** hat (I2C `0x54`)
- Transport: Classic Bluetooth — HID (mouse + keyboard) and HFP (mic)

## Control scheme (current)

| Input | Action |
| --- | --- |
| JoyC move | Move cursor |
| JoyC press | Left click |
| JoyC click, then hold & move (<450 ms window) | Scroll / pan |
| **BtnA double-tap** | Start voice dictation (device mic on + Ctrl+F5) |
| **BtnA single-tap** | Enter/send; if dictating, first stops voice (Ctrl+F5 paste) then sends. On Agents page: select thread → jump home |
| **BtnPWR (side button) tap** | **Escape** (cancel dictation + mic off) |
| BtnB tap | Cycle screens: Setup → Magic → **Agents** → Setup |
| **Agents page** | JoyC ↑/↓ = move selection · press = focus session · status dots (run/wait/idle/err) |
| BtnB hold 3 s / 8 s | Re-pair / clear bonds + reboot |
| Screen dot | 🔴 mouse · ⚪ mic active · 🟡 scroll |

## Changes from upstream

1. **HID keyboard now supports modifiers.** Upstream sent a 1-byte keyboard report (single keycode,
   no modifier). Extended the descriptor + report to 2 bytes `[modifier_bitmap, keycode]` so we can
   send `Ctrl+F5`. Generic `bt_input_key_send(mod, usage, pressed)` replaces the F15-only path;
   exposes `bt_input_dictation_tap()` (Ctrl+F5) and `bt_input_escape_tap()` (Esc).
2. **BtnA = mic + dictation trigger.** Tap toggles the device's own mic (HFP) and fires the Wispr
   shortcut in one gesture. (Replaced upstream's single-click-toggle / double-click-F15 split.)
3. **BtnPWR = context-dependent Escape/Enter.** While dictating it sends Escape (cancel + mic off);
   otherwise it sends Enter to submit/send (e.g. fire a message to an agent right after transcription).
4. **Joystick freeze fix.** When the JoyC MCU hangs while leaving the I2C bus idle, recovery now
   forces a real PortA power-cycle instead of re-probing forever; stuck-retry backoff cut 30s → 5s.
   (Previously required a manual device restart.)
5. **Agents page + live Codex feed (Phase 2–3).** A third screen (BtnB cycles to it) listing your
   Codex chats newest-first with status dots. A Mac companion (`companion/codex_agents.py`) reads
   `~/.codex` (read-only, metadata only) and **streams the list over USB-serial**; the firmware's
   `handle_agents_serial` task reads UART0 `@A|name~S|…` frames into `agents_model`. JoyC (or a BtnA
   click) selects a thread → jumps to the Magic home page to dictate. Falls back to demo data until
   the first serial frame. Files: `agents_model.[ch]`, `agents_serial.[ch]`, `ui/ui_agents_screen.[ch]`,
   `handle_agents_screen`, `companion/codex_agents.py`.
6. **Real thread switcher.** Selecting a chat sends `@SEL <conversationId>` back over serial; the
   companion fires `open codex://threads/<id>` to focus that thread in the Codex app (ChatGPT.app,
   bundle `com.openai.codex`, registers the `codex://` scheme). Frames carry the id as `name~S~id`.

### Running the companion
```sh
pip3 install pyserial          # once (or use the esp-idf env python, which has it)
python3 companion/codex_agents.py            # print the derived chat list
python3 companion/codex_agents.py --serial   # stream it to the stick over USB
```
Keep `--serial` running for live data. Status logic: `queued-follow-ups` → attention (yellow),
running process → green, else idle. Real Mac-side thread-focus on select is a later phase.

## Hardware constraints & decisions (hard-won — don't re-litigate)

- **JoyC and the internal mic share GPIO0 (PortA) — they cannot run at the same time.** Device mic on
  ⇒ joystick paused (returns when mic off). No firmware change avoids this; it's the wiring.
- **A Bluetooth mic is speech-grade, not hi-fi.** Mic audio only travels over HFP (~16 kHz wideband at
  best, phone-call quality). For high-quality dictation, a laptop/USB mic is better; the device mic is
  for handheld/remote use.
- **`Fn`/Globe cannot be sent over Bluetooth HID** — it's consumed inside Apple keyboards' own
  controller and has no transmittable HID usage. Fn+Space is impossible from any BT device. Use a
  real key (function key + modifier).
- **macOS maps F14/F15 to brightness down/up** by default → avoid bare F15 as a trigger. F13 has no
  default binding; the current trigger is **Ctrl+F5** (matches the shortcut already set in Wispr).
- **macOS reserves Ctrl+F5** by default for "Move focus to the window toolbar" (Keyboard > Keyboard
  Shortcuts > Keyboard). If focus jumps on tap, disable that shortcut or pick another combo.
- **Wispr's shortcut recorder rejects a bare function key** — it requires a modifier. Hence Ctrl+F5.
- **After any HID descriptor change, macOS caches the old layout** → keyboard input silently breaks
  while the mouse keeps working. Fix: Forget the device on the Mac + clear bonds (BtnB hold 8 s) +
  re-pair. This is a known macOS quirk (see upstream README).
- **Flashing: use `-b 115200`.** 460800 baud fails on this stick's USB-serial chip ("No serial data
  received" after the baud switch).

## Setup

1. **Wispr Flow:** set the dictation shortcut to **Ctrl+F5** (tap/toggle mode, not hold-to-talk).
   To use the device mic, select **"Magic Stick"** as the input (System Settings → Sound → Input, or
   in Wispr) while mic mode is on.
2. **Pairing:** pair "Magic Stick" over Bluetooth. **After flashing a descriptor change, re-pair**
   (Forget on Mac → BtnB hold 8 s on stick → BtnB hold 3 s → pair fresh).

## Build & flash

```sh
. ~/esp/esp-idf-v5.4.2/export.sh
idf.py build
idf.py -p /dev/cu.usbserial-XXXX -b 115200 flash   # 115200, NOT 460800
```

Host-side logic tests (no hardware): `cc -I main -I main/joystick test/test_<x>.c main/.../<x>.c -lm -o /tmp/t && /tmp/t`
(mouse_controller, button_action, joyc_button compile standalone; bt_* / mic_* need ESP-IDF headers).

## Roadmap / ideas (not built)

**Near-term (reuse the pipeline we have):**
- **Stabilize the Codex thread-switcher (IN PROGRESS — parked).** The *mechanism is proven*: running
  `open "codex://threads/<conversationId>"` in a terminal foregrounds the Codex app (ChatGPT.app,
  bundle `com.openai.codex`) — that's the "command sent when you click a thread". The full chain is
  wired (device sends `@SEL <id>` over serial → companion runs `open codex://...`). **Blocker:** the
  stick resets / drops off USB whenever the companion opens or closes the serial port (DTR/RTS
  toggle), so the back-channel is unreliable and validation kept failing. **Fix:** open the port
  *without* asserting DTR/RTS so the device doesn't reset (`stty -f <port> -clocal -hupcl` before
  open, or pyserial `dsrdtr`/manual line control), then validate press → `@SEL` → `open` end-to-end.
- **Messages page (voice-reply to iMessage).** Same architecture as the Agents page: companion reads
  `~/Library/Messages/chat.db` (needs Full Disk Access, read-only) → streams recent/unread threads to
  a new "Messages" screen → select a thread → companion focuses that conversation (AppleScript) →
  existing voice flow dictates the reply → single-click sends. "Clicky intelligence" (Claude-drafted
  quick replies) is a layer added later on top of the plumbing.
- **Remote page (IR).** The StickC Plus has a **built-in IR LED** → a fully on-device TV/AC/light
  remote as a 4th screen in the BtnB cycle; scroll IR commands with the JoyC, press to fire. No
  companion/network needed. (WiFi/smart-home — HomeKit, smart bulbs — is a separate, harder path
  needing network + per-vendor APIs; do IR first.)
- **Auto-start the companion** — a launchd/login item so the live feed is always on.

**Later:**
- **F13–F24 programmable palette** — on-screen strip of bindable keys (macOS Shortcuts / Raycast).
- **Media / meeting pages** — play-pause, volume slider (JoyC tilt), mute/camera toggle (needs an HID
  consumer-control report).
- **Richer agent status** — live running/done via `logs_2.sqlite`; extend beyond Codex (e.g. Claude).
