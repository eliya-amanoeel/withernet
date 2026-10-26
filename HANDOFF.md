# Hand-off: continuing on the PC

_Last updated: 2026-10-26, moving from the Mac to the PC._

## Where we are

- The repo is set up and pushed: README, [hardware notes](docs/hardware-notes.md), the
  [Nano check guide](docs/01-nano-check.md) and the `tools/nano-check` sketch. The sketch
  compiles but hasn't been uploaded yet.
- **Nano:** the Mac never saw it, because the only cable is USB-A → USB-C and the Mac has
  no USB-A port. On the PC the **power LED lights up**. Serial hasn't been tested yet.
- **Way of working:** you run every command and do every hardware step yourself. Claude
  only guides, one step at a time.

## Pick up here

1. Set up the PC:
   - `git clone https://github.com/eliya-amanoeel/withernet.git`
   - Install VS Code + the **PlatformIO IDE** extension.
2. **Step A** of the [Nano check](docs/01-nano-check.md): Device Manager → Ports
   (COM & LPT). Look for `USB-SERIAL CH340 (COMx)`. If it's under *Other devices* with
   a warning icon, install the WCH `CH341SER.EXE` driver.
3. **Steps B–D**: upload the sketch, run the serial echo test, then the pass-through
   loopback test (RESET→GND, D0→D1).
4. When all four pass, mark step 1 as done in the README status table.

## What comes after

- **Step 2:** the first serial flash of the WT32-ETH01 through the Nano. Wire it
  **straight through** (RX→RX0, TX→TX0, not crossed), add a voltage divider on the line
  into RX0, and jumper IO0→GND before powering on. Details are in the
  [hardware notes](docs/hardware-notes.md).
- **Firmware/OTA (not decided yet):** the plan is our own PlatformIO firmware with
  ArduinoOTA, so after the first USB flash every update is the **Upload** button in
  VS Code over Wi-Fi. Still to check: whether arduino-esp32 can do NAT routing
  between Wi-Fi (uplink) and Ethernet (downlink). If it can't, fall back to
  ESP-IDF or an existing NAT-router firmware.
- **Hardware still needed:** the WT32-ETH01 board(s), a USB cable for 5V power, and two
  resistors (1 kΩ + 2 kΩ) for the voltage divider.

## Gotchas to remember

- GPIO0 is both the boot pin and the 50 MHz Ethernet clock. Remove the IO0→GND jumper
  after flashing.
- Ethernet only starts if GPIO16 is driven high (it powers the oscillator).
- The dongle routes with NAT; it isn't a transparent bridge. Reaching the Nibe or
  Checkwatt *from* the home LAN needs port forwarding.
- ESPHome can't do the bridging (it runs Wi-Fi *or* Ethernet), so it's only useful if a
  board becomes a sensor.
