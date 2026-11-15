# Hand-off

_Last updated: 2026-11-15, on the Mac._

## Where we are

- **Both dongles are live** (since 2026-11-02): Nibe and Checkwatt show up in the Deco
  app, and the Powerline adapters are gone.
- **Installed:** both run `b23f606` (signed firmware, since 2026-11-15). Nibe
  `192.168.68.2`, Checkwatt `192.168.68.3`. Status page `http://<ip>/`, login `admin`
  with each dongle's own password (`ADMIN_PASS_NIBE` / `ADMIN_PASS_CHECKWATT` in
  `firmware/bridge/main/secrets.h`). Updates over Wi-Fi only, and they must be signed:
  see [Updating a dongle](#updating-a-dongle).
- **Why the project exists:** the Powerline sender sits outside the full 10" rack, cabled
  to the UniFi switch. With Wi-Fi the Deco XE75 is the sender, so it goes away.
- **How it works: MAC-cloning bridge, not NAT** (decided 2026-11-01). A transparent
  passthrough where the Deco assigns the device's IP, like the Powerline adapters did.
  See [README](README.md#how-it-works-mac-cloning).
- **Network:** fibre → master Deco XE75 (router, 3 units in mesh) → UniFi switch.
- **One dongle per device:** the Nibe and Checkwatt are in different places. Nibe is
  ~5 m from a Deco, Checkwatt ~3 m behind a wall. Both get about −47 dBm.
- **Ruled out:** a Deco satellite or extra Deco unit at the devices (not an option for
  us), UniFi Device Bridge (needs UniFi APs), NAT routing (router wouldn't see the
  devices).
- **Builds and Wi-Fi updates happen on the Mac** (since 2026-11-15), with ESP-IDF 5.3.1
  itself (`idf.py`), not PlatformIO: see [ESP-IDF on the Mac](#esp-idf-on-the-mac). The
  Mac's `main/secrets.h` is the current one; the PC's is out of date. Wired flashing was
  done on the PC with PlatformIO; those notes are marked "PC" below.
- **The "Nano" is an ATmega32U4 clone** (shows up as Arduino Leonardo, `2341:8036`), runs
  `tools/usb-serial-bridge` (still a PlatformIO sketch). Use the USB-C ↔ USB-C cable; the
  USB-A → USB-C one is charge-only. How to flash: [flashing guide](docs/01-flashing.md).
- **Way of working:** you run commands and do hardware steps yourself, and Claude
  guides one step at a time. Claude may run a test when you ask for it.

## Pick up here

Nothing planned. Still open:

- Check the Checkwatt shows up in the Deco app (it was slow to appear; it took a long
  time after the first install too).
- Seen once after an OTA reboot: `esp.emac: insufficient TX buffer size` (a Wi-Fi →
  Ethernet frame dropped in a burst). Harmless once; if it repeats, raise the EMAC TX
  buffers in `sdkconfig.defaults`.

Next feature, if wanted: the [setup hotspot](#what-comes-after).

## Updating a dongle

1. `tools/build-signed.sh nibe` (or `checkwatt`). It sets up ESP-IDF, builds into
   `firmware/bridge/build/<board>/`, signs with `~/.withernet/signing-key.pem` and checks
   the signature. It must end with `Signature is valid`.
2. On the dongle's status page, upload the `firmware-signed.bin` it printed. The dongle
   reboots into it (~30 s).
3. **Rollback:** a new firmware has 2 minutes after boot to get Wi-Fi up. If it doesn't
   (or crashes), the dongle reboots into the previous firmware by itself.

An unsigned or wrongly signed image is refused ("refused: not a valid firmware signed
with the withernet key") and the dongle keeps running. `BOARD=checkwatt-badwifi` is a
test build with a wrong Wi-Fi password, to check the rollback; never leave it installed.

## Security

**Signed firmware** (2026-11-15). A dongle accepts a Wi-Fi update only if it's signed
with the private key on the Mac, so a leaked admin password isn't enough to flash it.

- ESP-IDF "signed apps without Secure Boot": ECDSA, checked when an update is uploaded.
  The firmware carries the public key (`firmware/bridge/signature_verification_key.bin`,
  in git). The private key is `~/.withernet/signing-key.pem` on the Mac, never in git.
- **Losing the key** means a wired flash per dongle. You keep the backup.
- **Rollback with signing:** ESP-IDF's rollback call refuses to switch to an unsigned
  previous firmware, so the firmware then reboots and lets the bootloader roll back
  (the bootloader doesn't check signatures).
- Not protected: someone with the dongle in hand can flash anything over serial and read
  the Wi-Fi and admin passwords from flash. The admin password travels over plain HTTP
  (basic auth): fine on the home LAN, not for anything exposed.

**Scope (decided 2026-11-15):** signed firmware plus a strong, unique admin password per
dongle is enough. Don't widen the hardening beyond that.

- **No anti-rollback** (refusing older signed images): every signed image is our own,
  and it needs eFuses burned for good. Revisit only if an older build turns out to have
  a known hole.
- **No Secure Boot or flash encryption:** they only add protection against someone with
  the dongle in hand (that's the household), burn eFuses for good, and need a wired
  flash. Revisit only if a dongle ends up somewhere people outside the household can
  reach.

## How the firmware behaves

- Starts Ethernet (GPIO16 high, LAN8720) in promiscuous mode, learns the MAC from the
  first frame the device sends, sets it on the Wi-Fi station interface, joins the
  strongest Deco, then forwards frames both ways.
- Frames from any other MAC are ignored (logged once). Reboot to learn a new device.
- Joins the Deco **IoT network** (2.4 GHz only, WPA2). Name and password are in
  `main/secrets.h` (ignored by git); copy `main/secrets.example.h` for a new board.
- **Management IP:** a fixed IP of its own, outside the Deco's DHCP range, using the
  cloned MAC. Frames for that IP (and ARP for it) go to the dongle's own IP stack;
  everything else is forwarded. No IPv6. Set per board in
  `firmware/bridge/main/CMakeLists.txt`.
  - Deco DHCP pool: 192.168.68.50 – 192.168.71.250, router 192.168.68.1, mask
    255.255.252.0. Below the pool: `.2–.9` network devices (`.2`, `.3` the dongles),
    `.10–.12` and `.20–.26` Proxmox VMs. `.20–.49` is the homelab's Proxmox range and
    the NAS firewall blocks it.
- **Status page** `http://<mgmt ip>/` (Wi-Fi signal, learned MAC, link state, uptime,
  firmware upload); `/status.json` is the same data for scripts. Home Assistant will
  read `status.json`, so keep its format.
- Two OTA slots (`partitions.csv`). Changing the partition table needs a wired flash
  with erase.
- Wi-Fi power save is off: with it on, pings jumped to hundreds of ms.
- The serial log (115200) shows the Ethernet link speed and duplex on link up.
- **Known:** right after the link comes up, the device's first burst of traffic drops some
  Ethernet → Wi-Fi frames (127 in the first 30 s on the Mac, none after). Fine for the
  Nibe; tune only if it causes trouble.
- No status LED: the WT32-ETH01 has only a power LED. Watch the serial log instead.
- No setup hotspot (future feature). If a dongle can't join Wi-Fi, it needs a wired flash.

## History

- **2026-11-02: bench test and install.** With the Mac on Ethernet behind the dongle
  (Wi-Fi off), the Mac got `192.168.68.74` from the Deco, reached the internet, and
  showed up in the Deco app under its own MAC. Ping to the router: 0% loss, mostly
  2–10 ms with spikes to ~80 ms. Both dongles installed.
- **Management IP and status page + OTA** (step 5): built on the PC, bench tested with
  the Mac, then the last wired flash per dongle (with erase, for the new partition
  table). First on `.40`/`.41`.
- **2026-11-15: management IPs moved** to `.2`/`.3` over Wi-Fi, with a build per board.
- **2026-11-15: signed firmware + admin password per dongle.** Tested on the Checkwatt
  (no spare board): the signed `8b9a073-dirty` build was accepted over the old firmware;
  an unsigned image and one signed with another key were both refused without a reboot;
  the `checkwatt-badwifi` build rolled back by itself. Then `b23f606` went on the
  Checkwatt, and twice on the Nibe (the first upload went into the old firmware, which
  accepted anything; the second makes sure both slots hold firmware that checks
  signatures). The refusal wasn't repeated on the Nibe (same firmware).
- **2026-11-15: PlatformIO → ESP-IDF.** With signed apps on, PlatformIO's ESP-IDF build
  broke (it doesn't hand the public key to the bootloader build and leaves out the
  signed-image padding), and on the Mac its Intel CMake can't run Apple's `git`, so the
  firmware version became `1`. Both needed workarounds; ESP-IDF's own build has neither
  problem.

## What comes after

- **Future feature: setup hotspot.** Without it, a Wi-Fi password change means taking
  both dongles back to the Nano. Proposed design (not started):
  - After ~5 min of failing to join, open a hotspot `withernet-<last 4 of MAC>`, with a
    password from `main/secrets.h`. Keep retrying the old network meanwhile, so a Deco
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

## ESP-IDF on the Mac

One-time setup (Espressif's steps for 5.3.1, without git history: about 1–1.5 GB; with
history it's 4–5 GB):

```sh
brew install cmake ninja dfu-util python@3.13
mkdir -p ~/esp
git clone -b v5.3.1 --recursive --depth 1 --shallow-submodules https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.3.1
PATH="/opt/homebrew/opt/python@3.13/libexec/bin:$PATH" ~/esp/esp-idf-v5.3.1/install.sh esp32
```

ESP-IDF runs on Homebrew's Python 3.13, not the Mac's own 3.9: on 3.9 its dependency
check fails and `idf.py` refuses to build. So `install.sh` runs with 3.13 first in the
PATH (the last line above).

`tools/build-signed.sh <board>` sets up ESP-IDF itself. For other `idf.py` commands
(wired flashing), set it up in the terminal first: `. ~/withernet/tools/idf-env.sh`. The
signing tool (`espsecure.py`) comes with ESP-IDF.

## Gotchas to remember

- The bridge relies on **undocumented ESP-IDF calls** (`esp_wifi_internal_tx`,
  `esp_wifi_internal_reg_rxcb`) that have changed between IDF versions. The version is
  pinned to 5.3.1 in `firmware/bridge/CMakeLists.txt` (the build stops on any other).
  Don't upgrade casually.
- **One device per dongle.** A switch behind it won't work: only the cloned MAC can talk.
- If the ESP keeps dropping off the Deco mesh, turn on the Deco's **IoT Network**
  (separate 2.4 GHz network).
- Don't put the dongle inside a metal cabinet (fuse box): run a short cable in instead.
- After changing `sdkconfig.defaults`, delete `firmware/bridge/build/` so the change
  applies (each board's `sdkconfig` lives in its build folder).
- The public key file must be called `signature_verification_key.bin` (ESP-IDF's
  default name).
- GPIO0 is both the boot pin and the 50 MHz Ethernet clock. Remove the IO0→GND jumper
  after flashing.
- Ethernet only starts if GPIO16 is driven high (it powers the oscillator).
- ESPHome can't do the bridging (it runs Wi-Fi _or_ Ethernet), so it's only useful if a
  board becomes a sensor.
- Bench test from the Mac: if it gets a non-`192.168.68–71.x` address, its Ethernet is
  set to a manual IP. Set Configure IPv4 to "Using DHCP".

**Wired flashing** (see the [flashing guide](docs/01-flashing.md)):

- The bridge sketch has no DTR/RTS auto-reset, so entering the ESP32 bootloader is
  manual: IO0→GND, then power-cycle (or pulse EN to GND).
- **Uploads need esptool's `no_reset`** (`CONFIG_ESPTOOLPY_BEFORE_NORESET` in
  `sdkconfig.defaults`; on the PC it was `board_upload.before_reset` in PlatformIO). The
  Nano's USB serial drops data while DTR/RTS are low, and esptool's default reset leaves
  them low. Without it: `Failed to connect to ESP32: No serial data received`.
- If an upload fails with `No serial data received` even with `no_reset`, check the board
  is in download mode (monitor open, touch EN to GND, expect `waiting for download`),
  then retry. The second board's first attempt failed this way and the retry worked.
- The serial monitor misses the boot log if the ESP started before it opened. Touch EN
  to GND to restart it with the monitor open.
- **No divider in the flashing setup:** Nano `TX` (5 V) goes straight into ESP `RX0`
  (3.3 V). It works, but each flashing session stresses the pin. Only matters while the
  Nano is connected; an installed dongle on its own power supply doesn't need it. Add the
  1 kΩ/2 kΩ divider to the flashing wiring if the boards will be flashed a lot.
- **Mac:** `pio` (only for the Nano sketch) isn't on the PATH; use
  `~/.platformio/penv/bin/pio`. Wired flashing with `idf.py` hasn't been tried on the
  Mac yet.
- **PC:** only 115200 baud uploads worked.
- **PC:** `pio run -t erase` ignored `no_reset`; erasing needed esptool directly:
  `& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port COM6 --baud 115200 --before no_reset erase_flash`
- **PC:** `pio` isn't on the PATH in a normal terminal. Use a PlatformIO terminal or
  `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"`.
- **PC:** don't bench test from the PC's Ethernet: it's bound to a Hyper-V external
  switch, and traffic didn't get through (probably the dongle learned a MAC Windows
  doesn't use, not confirmed). Use the Mac.
