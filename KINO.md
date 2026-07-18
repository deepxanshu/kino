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
| **BtnPWR (side button)** | click = **Escape** (only while dictating) · ~6s hold = hardware power off (for battery) |
| BtnB tap | Cycle screens: Setup → Magic → **Agents** → Setup |
| **Agents page** | JoyC ↑/↓ = move selection · press (or BtnA click) = focus thread → jumps home · status = colour dot only (🟢 running · 🟡 waiting · 🔴 error · 🔵 idle), truncated thread name |
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
It **auto-starts** via a launchd agent (`companion/com.deepxanshu.kino-companion.plist`, installed to
`~/Library/LaunchAgents/`, KeepAlive + RunAtLoad, uses `companion/.venv`). So whenever the stick is
plugged in, the feed runs — no manual step, and it survives reboots/unplugs (auto-reconnects).
```sh
# manual run / debugging:
companion/.venv/bin/python companion/codex_agents.py           # print the open-thread list
companion/.venv/bin/python companion/codex_agents.py --serial  # stream to the stick
# manage the agent:
launchctl load -w  ~/Library/LaunchAgents/com.deepxanshu.kino-companion.plist   # enable
launchctl unload   ~/Library/LaunchAgents/com.deepxanshu.kino-companion.plist   # disable
tail -f /tmp/kino-companion.log                                                  # logs
```
Data = the app's open threads (`thread-writable-roots`). Live feed needs the stick on **USB** (serial);
untethered on Bluetooth carries voice/cursor/keys but not the thread list. No demo fallback — if no
feed, the screen shows "waiting for feed".

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
3. **WiFi feed (cable-free agent list):** put 2.4 GHz creds in `main/wifi_config.h` (gitignored — run
   `python3 companion/set_wifi_pass.py` for the password) and turn OFF the router's "client isolation".
   The stick joins WiFi (coexists with BT), advertises `kino.local`, runs a TCP server on 5010. The
   companion auto-starts over WiFi via launchd; manual: `companion/.venv/bin/python companion/codex_agents.py --net`
   (`--serial` = USB fallback). **RAM note:** WiFi+BT+LVGL is tight on the ESP32 — to fit it took WiFi
   IRAM-opt OFF, WiFi buffers cut (4/8/8) + AMPDU off, a **3 MB app partition** (`partitions.csv`), and
   dropping the USB-serial reader task. Data (and flashing) still work over USB too.

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
- **Codex thread-switcher — validating.** Mechanism proven (`open "codex://threads/<id>"` foregrounds
  the app). Chain wired: device sends `@SEL <id>` over serial → companion runs `open codex://...`.
  Reset-churn fix IN (DTR/RTS deasserted + HUPCL cleared, no reboot on connect/disconnect). Firmware
  fix IN (both joystick press and BtnA click fire the focus). **Data source fixed:** the companion now
  reads the app's actual open threads (`thread-writable-roots` + `local-projects` names + first-real-
  message titles + REAL conversationIds) instead of stale session files — so ids resolve. **Left:**
  confirm press → `@SEL` → thread focuses, end-to-end on device.
- **Diagnose blinking / joystick drops.** Reported: screen blinks randomly, joystick sometimes stops.
  Storage/flash is fine (app uses ~74% of its partition). A tethered capture showed only ONE clean
  POWERON reset (no crash loop / brownout in that window). Leading hypothesis: **brownout reboots on
  battery** — BT-TX + LCD current spikes dip the ~120mAh cell below the brownout threshold, resetting
  the ESP (screen blink + joystick reset). Also possible: JoyC I2C freeze (recovery exists). Next:
  capture serial while it happens and read `reset_reason`/`rst:` (brownout vs panic vs clean); if
  brownout, lower LCD brightness / BT-TX power or keep it charged.
- **Messages page (voice-reply to iMessage).** Same architecture as the Agents page: companion reads
  `~/Library/Messages/chat.db` (needs Full Disk Access, read-only) → streams recent/unread threads to
  a new "Messages" screen → select a thread → companion focuses that conversation (AppleScript) →
  existing voice flow dictates the reply → single-click sends. "Clicky intelligence" (Claude-drafted
  quick replies) is a layer added later on top of the plumbing.
- **Remote page (IR).** The StickC Plus has a **built-in IR LED** → a fully on-device TV/AC/light
  remote as a 4th screen in the BtnB cycle; scroll IR commands with the JoyC, press to fire. No
  companion/network needed. (WiFi/smart-home — HomeKit, smart bulbs — is a separate, harder path
  needing network + per-vendor APIs; do IR first.)

**Later:**
- **F13–F24 programmable palette** — on-screen strip of bindable keys (macOS Shortcuts / Raycast).
- **Media / meeting pages** — play-pause, volume slider (JoyC tilt), mute/camera toggle (needs an HID
  consumer-control report).
- **Richer agent status** — live running/done via `logs_2.sqlite`; extend beyond Codex (e.g. Claude).
