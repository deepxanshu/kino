# StickC JoyMic

Firmware for an M5StickC-Plus with a JoyC HAT.

The device exposes a single Classic Bluetooth product name, `StickC JoyMic`:

- `Mouse` mode: JoyC controls a Bluetooth HID mouse, and JoyC press sends left click.
- `Mic` mode: internal StickC microphone feeds the Bluetooth HFP microphone path.
- `IMU` mode: screen shows the onboard IMU cube view.
- `Setup` mode: screen shows Bluetooth HID/HFP/audio status, battery, and pairing hint.

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
```
