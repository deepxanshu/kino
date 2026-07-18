#!/usr/bin/env python3
"""Prompt for the WiFi password and write it into main/wifi_config.h (gitignored).
Run:  python3 companion/set_wifi_pass.py   (type the password at the prompt)."""
import getpass
import pathlib

cfg = pathlib.Path(__file__).resolve().parent.parent / "main" / "wifi_config.h"
text = cfg.read_text()
if "YOUR_WIFI_PASSWORD_HERE" not in text:
    print("Placeholder not found -- password may already be set. Aborting to avoid overwrite.")
    raise SystemExit(0)

pw = getpass.getpass("WiFi password: ")
if not pw:
    print("No password entered; nothing changed.")
    raise SystemExit(1)

cfg.write_text(text.replace("YOUR_WIFI_PASSWORD_HERE", pw))
print(f"Saved into {cfg} (gitignored). You can now say 'done'.")
