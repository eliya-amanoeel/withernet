# Hand-off

_Last updated: 2026-11-02, on the PC._

## Where we are

- **Steps 1 and 2 are done.** The Nano works as a USB-serial adapter, and the WT32-ETH01
  has been flashed through it several times.
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
  `tools/usb-serial-bridge`, flashes at 460800 or lower. Use the USB-C ↔ USB-C cable; the
  USB-A → USB-C one is charge-only. How to flash: [flashing guide](docs/01-flashing.md).
- **Way of working:** you run commands and do hardware steps yourself, and Claude
  guides one step at a time. Claude may run a test when you ask for it.

## Pick up here

**Step 3 is done: the bench test passed (2026-11-02).** This board becomes the Nibe
dongle. With the Mac on Ethernet behind it (Wi-Fi off), the Mac got `192.168.68.74` from
the Deco, reached the internet, and showed up in the Deco app under its own MAC. Ping to
the router: 0% loss, mostly 2–10 ms with spikes to ~80 ms.

- The firmware (`firmware/bridge`) starts Ethernet (GPIO16 high, LAN8720) in promiscuous
  mode. It learns the MAC from the first frame the device sends, sets it on the Wi-Fi
  station interface, joins the strongest Deco, then forwards frames both ways.
- Frames from any other MAC are ignored (logged once). Reboot to learn a new device.
- Joins the Deco **IoT network** (2.4 GHz only, WPA2). Name and password are in
  `src/secrets.h` (ignored by git); copy `src/secrets.example.h` for a new board.
- Wi-Fi power save is off: with it on, pings jumped to hundreds of ms.
- The serial log (115200) shows the Ethernet link speed and duplex, plus a stats line
  every 10 s with frames forwarded and dropped per direction.
- **Known:** right after the link comes up, the device's first burst of traffic drops some
  Ethernet → Wi-Fi frames (127 in the first 30 s on the Mac, none after). Fine for the
  Nibe; tune only if it causes trouble.
- No status LED: the WT32-ETH01 has only a power LED. Watch the serial log instead.
- No OTA, status page or hotspot yet.

**Next: step 4**, installing the dongle at the Nibe (see below). Before that, add the
divider on `RX0` (see Gotchas).

## What comes after

- **Step 4:** install one dongle at the Nibe and one at the Checkwatt, then remove both
  Powerline adapters. Let it run for a week.
- **Step 5, extras, one at a time:**
  - **Management IP:** the dongle borrows the device's MAC, so it has no address of its
    own. Plan: a fixed IP outside the Deco's DHCP range that the dongle answers itself.
  - **OTA updates** (password protected), using that management IP.
  - **Setup hotspot:** if the dongle can't join the Wi-Fi, it opens its own
    password-protected hotspot for entering a new Wi-Fi name and password.
  - **Status page:** read-only or password protected. Wi-Fi signal, learned MAC, link state.
- **Power:** see [hardware notes](docs/hardware-notes.md#power). Running: USB charger →
  USB-C breakout → ESP `5V`/`GND`.
- **Hardware still needed:** 1 kΩ + 2 kΩ resistors for the TX divider, female-female
  jumper wires, a second WT32-ETH01 for the Checkwatt. Have: a USB breakout with pins
  (check it has 5.1 kΩ CC resistors if it's USB-C).

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
- **No divider on the PC setup yet:** Nano `TX` (5 V) goes straight into ESP `RX0`
  (3.3 V). It works, but it can slowly damage the pin. Add the 1 kΩ/2 kΩ divider.
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
