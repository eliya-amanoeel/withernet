# Hand-off

_Last updated: 2026-11-14, on the Mac._

## Where we are

- **Steps 1–4 are done, step 5 is next.** Both dongles (Nibe and Checkwatt) have been live
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

**Next actions (2026-11-15).** 5a and 5b are written and pushed but **never compiled**:
they were written on the Mac, which has no PlatformIO. Go one step at a time:

1. On the PC, add `MGMT_IP` (`"192.168.68.41"` for the Checkwatt, which goes first),
   `MGMT_NETMASK`, `MGMT_GATEWAY` and `ADMIN_PASS` to `src/secrets.h`; see
   `src/secrets.example.h`.
2. Delete `sdkconfig.wt32-eth01`, then build. Fix any compile errors.
3. Bench: Checkwatt dongle on the Nano, **Erase Flash**, then Upload.
4. Mac on Ethernet behind it (Wi-Fi off): serial log should show `management IP`, the Mac
   should get internet, and `http://192.168.68.41/` should show the status page (from
   another machine on the network).
5. Try an update over Wi-Fi from the page, check it comes back and says
   `new firmware confirmed`.
6. Reinstall it at the Checkwatt; then the same for the Nibe dongle with `.40`.

Watch for: the Deco app listing the device under the management IP instead of its own.

**Step 5: extras, in this order.** Each sub-step gets a bench test on a spare dongle (or
with the Mac behind it) before it goes into the installed ones.

1. **5a. Management IP.** The dongle answers on a fixed IP of its own, outside the Deco's
   DHCP range, using the cloned MAC. Frames for that IP (and ARP for it) go to the
   dongle's own IP stack; everything else is forwarded as before. Test: ping it from the
   Mac while the device behind it keeps working.
   - Deco DHCP pool: 192.168.68.50 – 192.168.71.250, router 192.168.68.1, mask
     255.255.252.0. In use below the pool: .10–.12 and .20–.26 (Proxmox VMs).
   - **Nibe dongle: 192.168.68.40. Checkwatt dongle: 192.168.68.41.** Set per board in
     `src/secrets.h` (`MGMT_IP`) before flashing.
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
3. **5c. Setup hotspot.** If the dongle can't join the Wi-Fi, it opens its own
   password-protected hotspot for entering a new Wi-Fi name and password. Delivered OTA.

**Installed dongles:** the Nibe one runs the build from just before the tidy-up (same
behaviour, plus a stats line in the serial log every 10 s). The Checkwatt one runs the
current build. Both get 5b over the wire, then everything later over the air.

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
- No setup hotspot yet. Status page and OTA are step 5b; until a dongle has 5b, any
  change means a wired flash.

## What comes after

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
