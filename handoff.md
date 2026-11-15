# Hand-off

_Last updated: 2026-11-15, on the PC._

## Where we are

- **Steps 1–5 are done** (5c, the setup hotspot, is parked as a future feature). Both dongles (Nibe and Checkwatt) have been live
  since 2026-11-02, both devices show up in the Deco app, and the Powerline adapters are
  gone.
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

**Done (2026-11-15): management IPs moved** to Nibe `192.168.68.2`, Checkwatt
`192.168.68.3` (`.20–.49` is the homelab's Proxmox range and the NAS firewall blocks it;
`.2–.9` is now the block for network devices). Each dongle has its own build target:
`pio run -e nibe` / `pio run -e checkwatt` (IP in `platformio.ini`, not `secrets.h`), and
the status page heading and `status.json` show the board name and IP. Both updated over
Wi-Fi and checked: Ethernet and Wi-Fi up, device behind it online.

**Done (2026-11-15):** 5a + 5b built on the PC, bench tested with the Mac (internet OK,
status page OK, Mac listed in the Deco under its own IP `.74`, OTA update confirmed), and
flashed into both dongles (then on `.40`/`.41`, since moved to `.2`/`.3`); both show
Ethernet and Wi-Fi up. No more wired flashes: update via `http://<mgmt ip>/`.

**Next:** check the Checkwatt shows up in the Deco app (it was slow to appear; it took a
long time after the first install too). Nothing else planned; see
[What comes after](#what-comes-after) for the setup hotspot.

Seen once after an OTA reboot: `esp.emac: insufficient TX buffer size` (a Wi-Fi → Ethernet
frame dropped in a burst). Harmless once; if it repeats, raise the EMAC TX buffers in
`sdkconfig.defaults`.

**Step 5: extras (done).** Each sub-step gets a bench test on a spare dongle (or
with the Mac behind it) before it goes into the installed ones.

1. **5a. Management IP.** The dongle answers on a fixed IP of its own, outside the Deco's
   DHCP range, using the cloned MAC. Frames for that IP (and ARP for it) go to the
   dongle's own IP stack; everything else is forwarded as before. Test: ping it from the
   Mac while the device behind it keeps working.
   - Deco DHCP pool: 192.168.68.50 – 192.168.71.250, router 192.168.68.1, mask
     255.255.252.0. In use below the pool: .2–.3 (dongles), .10–.12 and .20–.26 (Proxmox VMs).
   - **Nibe dongle: 192.168.68.2. Checkwatt dongle: 192.168.68.3.** Set per board in
     `platformio.ini` (`env:nibe`, `env:checkwatt`); build with `pio run -e <board>`.
   - The dongle has no IPv6 and only answers IPv4 + ARP for its own IP.
2. **5b. Status page + OTA.** A small password-protected web server on the management IP:
   a status page (Wi-Fi signal, learned MAC, link state, uptime) and a firmware upload.
   This is the **last wired flash** per dongle; after it, updates go over Wi-Fi.
   - `http://<mgmt ip>/` is the status page with the upload form, and `/status.json` is
     the same data for scripts. Login `admin`, password `ADMIN_PASS` in `src/secrets.h`.
     Plain HTTP with basic auth: fine on the home LAN, not for anything exposed.
   - The partition table changes to two OTA slots (`partitions.csv`), so this flash
     needs **Erase Flash first**, then Upload. Delete `sdkconfig.wt32-eth01` before
     building so the new `sdkconfig.defaults` apply.
   - **Rollback:** a new firmware has 2 minutes after boot to get Wi-Fi up. If it
     doesn't (or crashes), the dongle reboots into the previous firmware.

**Installed dongles:** both run the per-board build (`29fcdbc` + this change), Nibe
`192.168.68.2`, Checkwatt `192.168.68.3`. Login `admin` / `ADMIN_PASS`.

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
- No setup hotspot (future feature). If a dongle can't join Wi-Fi, it needs a wired flash.
- Bench test gotcha: if the Mac gets a non-`192.168.68–71.x` address, its Ethernet is set
  to a manual IP. Set Configure IPv4 to "Using DHCP".

## What comes after

- **Future feature: setup hotspot (was 5c).** Without it, a Wi-Fi password change means
  taking both dongles back to the Nano. Proposed design (not started):
  - After ~5 min of failing to join, open a hotspot `withernet-<last 4 of MAC>`, with a
    password from `src/secrets.h`. Keep retrying the old network meanwhile, so a Deco
    reboot or short outage doesn't strand it on the hotspot.
  - Setup page at `192.168.4.1`: Wi-Fi name + password, saved to NVS (overriding
    `secrets.h`), then reboot.
  - The 2 min OTA rollback fires before the 5 min hotspot, so a bad update still rolls
    back first.
  - Delivered OTA; test on the Checkwatt first by setting a wrong password.
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
- **`pio run -t erase` ignores `no_reset`** and fails with `No serial data received`.
  Erase with esptool directly instead (EN to GND first, use the Nano's port):
  `& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port COM6 --baud 115200 --before no_reset erase_flash`
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
