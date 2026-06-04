# Magic Stick

Firmware for an M5StickC-Plus with a JoyC HAT.

The device exposes a single Classic Bluetooth product name, `Magic Stick`.

The UI has two pages:

- `Setup`: Bluetooth HID/HFP status, HFP audio channel status, battery, and pairing status.
- `Magic`: JoyC mouse control, IMU cube, mic icon, mic spectrum, and segmented battery status.

On the Magic page, BtnA single-click waits for the double-click window and then toggles between:

- `Mic off / Joystick on`: JoyC controls the Bluetooth HID mouse, and JoyC press sends left click.
- `Mic on / Joystick off`: internal StickC microphone feeds the Bluetooth HFP microphone path.

BtnA double-click within 450 ms toggles between the same two states and sends one macOS F15 tap over HID.
Because the HID descriptor includes an F15 keyboard report, macOS may need the `Magic Stick` pairing removed and recreated after flashing this firmware.

In joystick mode, click once and hold the second click within 450 ms to enter scroll mode. While holding,
push the JoyC up/down for vertical scrolling or left/right for horizontal scrolling.

JoyC and the internal mic both need PortA GPIO0/GPIO26 resources, so the firmware switches those peripherals instead of running both at the same time.

## Hardware

- M5StickC-Plus
- M5Stack JoyC HAT at I2C address `0x54`

## Build Environment

- ESP-IDF v5.4.2
- ESP32 target

## Build And Flash

```sh
. /Users/xurui/esp/esp-idf-v5.4.2/export.sh
idf.py build
idf.py -p /dev/cu.usbserial-49523D2FE2 -b 115200 flash
```

## Host Test

```sh
cc -I main/mic test/test_mic_spectrum.c main/mic/mic_spectrum.c -lm -o /tmp/test_mic_spectrum
/tmp/test_mic_spectrum
cc -I main/bluetooth test/test_bt_pairing_status.c main/bluetooth/bt_pairing_status.c -o /tmp/test_bt_pairing_status
/tmp/test_bt_pairing_status
cc -I main -I main/joystick test/test_button_action.c main/button_action.c -o /tmp/test_button_action
/tmp/test_button_action
```
