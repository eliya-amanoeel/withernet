# Hand-off

_Last updated: 2026-11-01, on the Mac._

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

**Step 3: minimal bridge firmware**, in `firmware/bridge`:

- PlatformIO, **ESP-IDF** framework (not Arduino): the bridge needs low-level Wi-Fi calls.
- Wi-Fi name and password in `secrets.h`, ignored by git.
- Start Ethernet (GPIO16 high, LAN8720), wait for the first frame from the device and
  learn its MAC.
- Set that MAC on the Wi-Fi station interface, join the Deco.
- Forward frames both ways, unchanged.
- Onboard LED shows link status; serial log at 115200.
- **Nothing else yet**: no OTA, status page or hotspot.

**Bench test:** dongle into the laptop's Ethernet port. Pass = the laptop gets an IP from
the Deco and shows up in the Deco app under its own MAC.

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
- GPIO0 is both the boot pin and the 50 MHz Ethernet clock. Remove the IO0→GND jumper
  after flashing.
- Ethernet only starts if GPIO16 is driven high (it powers the oscillator).
- ESPHome can't do the bridging (it runs Wi-Fi _or_ Ethernet), so it's only useful if a
  board becomes a sensor.
