# Hand-off

_Last updated: 2026-10-31, on the Mac._

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
- **WT32-ETH01 is here:** board says "ESP32-ETH01 V1.4", header pins already soldered,
  no USB port (so the first flash has to go through the Nano).
- **Way of working:** you run commands and do hardware steps yourself, and Claude
  guides one step at a time. Claude may run a test when you ask for it.

## Pick up here

**Step 2:** the first serial flash of the WT32-ETH01 through the Nano. Because the Nano
runs the bridge sketch, wire it **crossed**: Nano TX (D1) → divider → RX0, Nano RX (D0)
→ TX0, plus 5V and GND. Jumper IO0→GND before powering on, and flash at **460800 or
lower**. Details are in the [hardware notes](docs/hardware-notes.md).

## What comes after

- **Firmware: we build our own (decided 2026-10-31).** A ready-made option exists
  (`martin-ger/esp32_ethernet_router`, ESP-IDF, lists WT32-ETH01 support), but there's
  no widely used standard for Wi-Fi → Ethernet on ESP32, so we write our own. The plan is PlatformIO with
  ArduinoOTA, so after the first USB flash every update is the **Upload** button in
  VS Code over Wi-Fi. Still to check: whether arduino-esp32 can do NAT routing
  between Wi-Fi (uplink) and Ethernet (downlink). If it can't, fall back to
  ESP-IDF or an existing NAT-router firmware.
- **Management (decided 2026-10-31):**
  - **Status page:** read-only (or behind a password). Shows Wi-Fi signal, IP
    addresses, and whether the Nibe/Checkwatt is connected.
  - **Setup hotspot:** if the dongle can't join the home Wi-Fi, it opens its own
    password-protected hotspot with a page for entering the Wi-Fi name and password.
    No reflashing when the Wi-Fi password changes.
  - **OTA updates** need a password.
- **Power:** see [hardware notes](docs/hardware-notes.md#power). Flashing: the Nano
  powers the ESP. Running: USB charger → USB-C breakout → ESP `5V`/`GND`.
- **Security (to do in the firmware):** the three passwords above. NAT already keeps
  the Nibe/Checkwatt unreachable from the home LAN; whether to open the Nibe's local
  page via port forwarding is still to discuss.
- **Hardware still needed:** two resistors (1 kΩ + 2 kΩ) for flashing, female-female
  jumper wires. Have: a USB breakout with pins (check it has 5.1 kΩ CC resistors if
  it's USB-C).

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
