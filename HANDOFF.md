# Hand-off

_Last updated: 2026-10-26, on the PC._

## Where we are

- **Step 1 (Nano check) is done.** All four checks in the [Nano check guide](docs/01-nano-check.md)
  passed on the PC.
- **The "Nano" is an ATmega32U4 clone**, not a classic ATmega328P + CH340. Windows lists
  it as **Arduino Leonardo** (USB ID `2341:8036`), currently on **COM6**. The COM number
  can change after uploads; `pio device list` shows the current one.
- **Cable:** the USB-A → USB-C cable is charge-only (that's also why the Mac never saw
  the board). Use the USB-C ↔ USB-C cable.
- **The Nano currently runs `tools/usb-serial-bridge`**, which forwards USB ↔ D0/D1. The
  loopback test passed at 115200, 230400 and 460800 baud; 921600 does not work.
- **Way of working:** you run commands and do hardware steps yourself, and Claude
  guides one step at a time. Claude may run a test when you ask for it.

## Pick up here

**Step 2:** the first serial flash of the WT32-ETH01 through the Nano. Because the Nano
runs the bridge sketch, wire it **crossed**: Nano TX (D1) → divider → RX0, Nano RX (D0)
→ TX0, plus 5V and GND. Jumper IO0→GND before powering on, and flash at **460800 or
lower**. Details are in the [hardware notes](docs/hardware-notes.md).

## What comes after

- **Firmware/OTA (not decided yet):** the plan is our own PlatformIO firmware with
  ArduinoOTA, so after the first USB flash every update is the **Upload** button in
  VS Code over Wi-Fi. Still to check: whether arduino-esp32 can do NAT routing
  between Wi-Fi (uplink) and Ethernet (downlink). If it can't, fall back to
  ESP-IDF or an existing NAT-router firmware.
- **Hardware still needed:** the WT32-ETH01 board(s), a USB cable for 5V power, and two
  resistors (1 kΩ + 2 kΩ) for the voltage divider.

## Gotchas to remember

- The bridge sketch has no DTR/RTS auto-reset, so entering the ESP32 bootloader is
  manual: IO0→GND, then power-cycle (or pulse EN to GND).
- GPIO0 is both the boot pin and the 50 MHz Ethernet clock. Remove the IO0→GND jumper
  after flashing.
- Ethernet only starts if GPIO16 is driven high (it powers the oscillator).
- The dongle routes with NAT; it isn't a transparent bridge. Reaching the Nibe or
  Checkwatt *from* the home LAN needs port forwarding.
- ESPHome can't do the bridging (it runs Wi-Fi *or* Ethernet), so it's only useful if a
  board becomes a sensor.
