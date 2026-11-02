# Hand-off

_Last updated: 2026-11-02, on the PC._

## Where we are

- **Steps 1–3 are done, step 4 is under way.** Both WT32-ETH01 boards are flashed and
  bench tested. Both dongles (Nibe and Checkwatt) have been live since 2026-11-02.
- **The plan changed (2026-11-01): MAC-cloning bridge, not NAT.** The goal is a
  transparent passthrough where the Deco assigns the device's IP, like the Powerline
  adapters did. See [README](README.md#how-it-works-mac-cloning).
- **Why the project exists:** the Powerline sender sits outside the full 10" rack, cabled
  to the UniFi switch. With Wi-Fi the Deco XE75 is the sender, so it goes away.
- **Network:** fibre → master Deco XE75 (router, 3 units in mesh) → UniFi switch.
- **One dongle per device:** the Nibe and Checkwatt are in different places. Nibe is
  ~5 m from a Deco, Checkwatt ~3 m behind a wall. Signal isn't expected to be a problem.
- **Ruled out:** a Deco satellite or extra Deco unit at the devices (not an option for
  us), UniFi Device Bridge (needs UniFi APs), NAT routing (router wouldn't see the
  devices).
- **The "Nano" is an ATmega32U4 clone** (shows up as Arduino Leonardo, `2341:8036`), runs
  `tools/usb-serial-bridge`, flashes at 115200 on the PC. Use the USB-C ↔ USB-C cable; the
  USB-A → USB-C one is charge-only. How to flash: [flashing guide](docs/01-flashing.md).
- **Way of working:** you run commands and do hardware steps yourself, and Claude
  guides one step at a time. Claude may run a test when you ask for it.

## Pick up here

**Step 4: installing and watching (from 2026-11-02).**

- **Nibe dongle:** installed on its own power supply and live since 2026-11-02. The Nibe
  shows up in the Deco app. It runs the build from just before the tidy-up (same
  behaviour, plus a stats line in the serial log every 10 s); reflash only if it comes
  back to the Nano anyway.
- **Checkwatt dongle:** flashed with the current build, bench tested with the Mac (link
  up, MAC learned, Wi-Fi connected), and installed on its own power supply on 2026-11-02.
- **Next:** let both run until about 2026-11-09. If they stay stable (visible in the Deco
  app, the Nibe and Checkwatt apps don't drop out), remove the Powerline adapters.

**Bench test (2026-11-02):** with the Mac on Ethernet behind the dongle (Wi-Fi off), the
Mac got `192.168.68.74` from the Deco, reached the internet, and showed up in the Deco app
under its own MAC. Ping to the router: 0% loss, mostly 2–10 ms with spikes to ~80 ms.

- The firmware (`firmware/bridge`) starts Ethernet (GPIO16 high, LAN8720) in promiscuous
  mode. It learns the MAC from the first frame the device sends, sets it on the Wi-Fi
  station interface, joins the strongest Deco, then forwards frames both ways.
- Frames from any other MAC are ignored (logged once). Reboot to learn a new device.
- Joins the Deco **IoT network** (2.4 GHz only, WPA2). Name and password are in
  `src/secrets.h` (ignored by git); copy `src/secrets.example.h` for a new board.
- Wi-Fi power save is off: with it on, pings jumped to hundreds of ms.
- The serial log (115200) shows the Ethernet link speed and duplex on link up.
- **Known:** right after the link comes up, the device's first burst of traffic drops some
  Ethernet → Wi-Fi frames (127 in the first 30 s on the Mac, none after; measured with
  temporary counters, since removed). Fine for the Nibe; tune only if it causes trouble.
- No status LED: the WT32-ETH01 has only a power LED. Watch the serial log instead.
- No OTA, status page or hotspot yet. Adding any of them means one more wired flash per
  dongle; after OTA, updates go over Wi-Fi.

## What comes after

- **Step 5, extras, one at a time:**
  - **Management IP:** the dongle borrows the device's MAC, so it has no address of its
    own. Plan: a fixed IP outside the Deco's DHCP range that the dongle answers itself.
  - **OTA updates** (password protected), using that management IP.
  - **Setup hotspot:** if the dongle can't join the Wi-Fi, it opens its own
    password-protected hotspot for entering a new Wi-Fi name and password.
  - **Status page:** read-only or password protected. Wi-Fi signal, learned MAC, link state.
- **Power:** see [hardware notes](docs/hardware-notes.md#power). Running: USB charger →
  USB-C breakout → ESP `5V`/`GND`.
- **Hardware:** nothing needed. Optional: 1 kΩ + 2 kΩ resistors for the flashing divider
  (only used while flashing through the Nano).

## Gotchas to remember

- The bridge relies on **undocumented ESP-IDF calls** (`esp_wifi_internal_tx`,
  `esp_wifi_internal_reg_rxcb`) that have changed between IDF versions. Once it works,
  pin the platform version in `platformio.ini` and don't upgrade casually.
- **One device per dongle.** A switch behind it won't work: only the cloned MAC can talk.
- If the ESP keeps dropping off the Deco mesh, turn on the Deco's **IoT Network**
  (separate 2.4 GHz network).
- Don't put the dongle inside a metal cabinet (fuse box): run a short cable in instead.
- The bridge sketch has no DTR/RTS auto-reset, so entering the ESP32 bootloader is
  manual: IO0→GND, then power-cycle (or pulse EN to GND).
- **Uploads need `board_upload.before_reset = no_reset`** (set in `platformio.ini`). The
  Nano's USB serial drops data to the PC while DTR/RTS are low, and esptool's default
  reset leaves them low. Without it: `Failed to connect to ESP32: No serial data received`.
- **On the PC, only 115200 baud uploads work** (`upload_speed` in `platformio.ini`).
- **No divider in the flashing setup:** Nano `TX` (5 V) goes straight into ESP `RX0`
  (3.3 V). It works, but each flashing session stresses the pin. Only matters while the
  Nano is connected; an installed dongle on its own power supply doesn't need it. Add the
  1 kΩ/2 kΩ divider to the flashing wiring if the boards will be flashed a lot.
- If an upload fails with `No serial data received` even with `no_reset`, check the board
  is in download mode (monitor open, touch EN to GND, expect `waiting for download`),
  then retry. The second board's first attempt failed this way and the retry worked.
- On the PC, `pio` isn't on the PATH in a normal terminal. Use a PlatformIO terminal or
  `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"`.
- The serial monitor misses the boot log if the ESP started before it opened. Touch EN
  to GND to restart it with the monitor open.
- Don't bench test from the PC's Ethernet: it's bound to a Hyper-V external switch, and
  traffic didn't get through (probably the dongle learned a MAC Windows doesn't use, not
  confirmed). Use the Mac.
- GPIO0 is both the boot pin and the 50 MHz Ethernet clock. Remove the IO0→GND jumper
  after flashing.
- Ethernet only starts if GPIO16 is driven high (it powers the oscillator).
- ESPHome can't do the bridging (it runs Wi-Fi _or_ Ethernet), so it's only useful if a
  board becomes a sensor.
