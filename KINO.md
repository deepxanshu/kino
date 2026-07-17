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
| **BtnA tap** | Toggle device mic on/off **and** send **Ctrl+F5** (Wispr Flow start/stop) |
| **BtnPWR (side button) tap** | Send **Escape** (cancel dictation), force mic off |
| BtnB tap | Toggle Setup ⇄ Magic screen |
| BtnB hold 3 s / 8 s | Re-pair / clear bonds + reboot |
| Screen dot | 🔴 mouse · ⚪ mic active · 🟡 scroll |

## Changes from upstream

1. **HID keyboard now supports modifiers.** Upstream sent a 1-byte keyboard report (single keycode,
   no modifier). Extended the descriptor + report to 2 bytes `[modifier_bitmap, keycode]` so we can
   send `Ctrl+F5`. Generic `bt_input_key_send(mod, usage, pressed)` replaces the F15-only path;
   exposes `bt_input_dictation_tap()` (Ctrl+F5) and `bt_input_escape_tap()` (Esc).
2. **BtnA = mic + dictation trigger.** Tap toggles the device's own mic (HFP) and fires the Wispr
   shortcut in one gesture. (Replaced upstream's single-click-toggle / double-click-F15 split.)
3. **BtnPWR = cancel.** The side power button sends Escape and forces mic off — no long-hold gesture.

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

- **F13–F24 programmable palette** — on-screen strip of buttons, each sending a bindable key
  (macOS Shortcuts / Raycast). Touch-Bar-style, most flexible; smallest firmware lift.
- **Media / meeting pages** — play-pause, volume slider (JoyC tilt), mute/camera toggle — needs an
  added HID consumer-control report.
- **Agent-state visualization** — the longer-term goal: show/act on coding-agent status on the screen.
