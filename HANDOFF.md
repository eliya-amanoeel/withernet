# Hand-off

_Last updated: 2026-11-01, on the PC._

## Where we are

- **Step 1 (Nano check): done.** See the [Nano check guide](docs/01-nano-check.md).
- **Step 2 (first serial flash): done.**
- **Step 3 (router firmware): mostly working, not finished.** The ESP joins Wi-Fi, hands
  out an address on the cable, and forwards traffic out with NAT. Replies coming back
  through it are not confirmed yet (see "Next step").
- **The "Nano" is an ATmega32U4 clone** (Windows: **Arduino Leonardo**, USB ID `2341:8036`,
  currently **COM6**; `pio device list` shows the current port). It runs
  `tools/usb-serial-bridge`, a USB-serial adapter for flashing and the monitor.
- **Cable:** the USB-A → USB-C cable is charge-only. Use the USB-C ↔ USB-C cable.
- **WT32-ETH01:** "ESP32-ETH01 V1.4", ESP32-D0WD-V3 rev 3, MAC `4c:c3:82:e8:cc:04`, 4 MB
  flash, no USB port.
- **Way of working:** you run commands and do hardware steps yourself. Claude guides
  **one step at a time, with short replies**, and proposes changes before making them.

## Current setup (2026-11-01)

| Wire | From | To |
|------|------|----|
| black | Nano `GND` | ESP `GND` (right column, above `5V`) |
| brown | Nano `TX` | ESP `RX0` |
| orange | Nano `RX` | ESP `TX0` |
| white | ESP `IO0` | ESP `GND`: **only while flashing** |
| — | **5V brick** | ESP `5V` / `GND` (powers the ESP) |

The red wire (Nano `VCC` → ESP `5V`) is **disconnected**: the Nano's 5V wasn't enough
once Ethernet and routing were running (see "Problems solved").

- ESP on the **main** Deco network (secrets.ini), IP **192.168.68.73**.
- Ethernet cable from the ESP to the desktop PC (adapter "vEthernet (External Switch)"),
  which gets **10.10.0.2**, gateway 10.10.0.1.

### How to flash and watch (from inside `firmware/`)

1. IO0 jumper on, power-cycle (unplug and replug the brick).
2. `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e serial -t upload`
3. Jumper off, start the monitor, then power-cycle (so you see the whole boot):
   `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor`

## What the firmware does now

[firmware/src/main.cpp](firmware/src/main.cpp):
- Scans, lists every node of our Wi-Fi network, and joins the **strongest**, locked to
  its BSSID. If Wi-Fi drops, it scans again every 15 s (no auto-reconnect).
- Prints the reason for every Wi-Fi disconnect.
- **Only after Wi-Fi is up**, starts Ethernet: cable side **10.10.0.1/24**, our own DHCP
  server (hands out 10.10.0.2+, gateway 10.10.0.1, DNS = the home DNS), then NAT once the
  cable link is up. Prints each address it hands out.
- Prints which interface internet traffic leaves by (must be `sta`), and checks whether
  the ESP itself reaches example.com.

## Next step

**Confirm replies come back through the ESP.** The Pi-hole log shows the PC's DNS
queries arriving from 192.168.68.73 (the ESP), so NAT works outbound. But
`ping -S 10.10.0.2 8.8.8.8` from the PC timed out. Planned test: switch the PC's Wi-Fi
off for a moment, so it uses only the cable, then run `ping -n 10 8.8.8.8` and
`nslookup example.com`, copy the output, and switch Wi-Fi back on.

## Unsolved problems

1. **Replies through NAT not confirmed** (see "Next step").
2. **The ESP's own DNS lookup fails.** At boot, `example.com` times out (error -54), and
   the query never shows up in the Pi-hole (192.168.68.71, which the Deco hands out as
   DNS). Note: the check runs before NAT is on and right after Ethernet starts.
3. **Wi-Fi reconnect while Ethernet runs can crash.** The ESP crashed inside the Wi-Fi
   driver's WPA3 (SAE) login while Ethernet was up. Starting Ethernet after Wi-Fi fixed it
   at boot, but a later reconnect still happens with Ethernet running. The firmware
   can't turn WPA3 off. Planned fix: on Wi-Fi loss, stop Ethernet (plus DHCP and NAT),
   and restart them once Wi-Fi is back.
4. **The 50 MHz Ethernet oscillator disturbs Wi-Fi** (hardware/RF, see the table below).
   Accepted for now (decided 2026-11-01): the Nibe and Checkwatt need little bandwidth.
   Revisit only if they have problems. Options then: shield the oscillator, or another
   board (external antenna, or a W5500 module without a 50 MHz oscillator).
5. **The Deco steers the ESP to a weak node** when left to itself. Worked around by
   locking to the strongest BSSID. Since the lock, the Deco app shows the ESP
   ("withernet").

Ping PC → ESP (50 pings, main network):

| Setup | Loss | Avg | Max |
|---|---|---|---|
| Ethernet on | 0% | 131 ms | 644 ms |
| Ethernet off | 0% | **12 ms** | 83 ms |
| Oscillator on, Ethernet driver off | 0% | 143 ms | 690 ms |

(The IoT network was worse still: 4–18% loss, 300+ ms.)

## Problems solved

- **Toolchain:** pioarduino `stable` (55.x) rejects PlatformIO Core 6.2.0.post1, so we
  pin 54.03.21. esptool 5.0.0 needs `click<8.2` in the PlatformIO penv.
- **Flashing through the 32U4:** it drops data while DTR is off, so `esptool.cfg` keeps DTR
  on, and so does `monitor_dtr`. Flash at 115200 only (the Nano's clock is 8.5% off at
  460800).
- **Wi-Fi power save** caused stalls: switched off in the firmware.
- **Crashes at boot** (WPA3 login with the cable up): Ethernet now starts after Wi-Fi.
- **Random crashes under traffic** (heap, watchdog, cache errors): **power**. The Nano's 5V
  sagged. On the 5V brick it's stable.
- **DNS 0.0.0.0:** lwIP keeps one DNS list for all interfaces, and setting up the cable
  side wiped it. The DNS is now read first and passed on.
- **No gateway on the PC:** the DHCP server only offers a gateway if the interface has one
  set, so the cable side now has itself (10.10.0.1) as gateway, and Wi-Fi is forced as
  the default route.
- **NAT failed to start:** lwIP only allows it while the cable link is up, so the firmware
  retries until it is.
- **Weak node / BEACON_TIMEOUT:** locked to the strongest node.

## Dropped topics

- **OTA (2026-10-31):** worked once, but too slow and unreliable on our Wi-Fi. Removed
  from the firmware. Now a future feature.
- **The IoT network:** worse than the main network. The ESP stays on the main network.
- **Test builds** `serial_no_eth` / `serial_osc_only`: done and removed.
- **Ready-made router firmware** (`martin-ger/esp32_ethernet_router`): not needed;
  arduino-esp32 3.x can do NAT and DHCP itself.

## What comes after

- **Finish step 3:** unsolved problems 1–3 above, then test with the Nibe/Checkwatt.
- **Management (decided 2026-10-31):**
  - **Status page:** read-only (or behind a password). Shows Wi-Fi signal, IP
    addresses, and whether the Nibe/Checkwatt is connected.
  - **Setup hotspot:** if the dongle can't join the home Wi-Fi, it opens its own
    password-protected hotspot with a page for entering the Wi-Fi name and password.
- **Future feature: OTA updates over Wi-Fi.** Needs a password, a timeout above the 1 s
  default (we used 10 s), Wi-Fi power save off, and the IP instead of `withernet.local`
  (mDNS took ~40 s on Windows). The final `WinError 10054` after `100% Done...` is
  harmless.
- **Power:** see [hardware notes](docs/hardware-notes.md#power). The ESP needs a proper 5V
  supply when running (the Nano's 5V isn't enough). Planned: USB charger → USB-C
  breakout → ESP `5V`/`GND`.
- **Security:** passwords for the status page and the setup hotspot (and OTA, if it
  comes back). NAT already keeps the Nibe/Checkwatt unreachable from the home LAN;
  whether to open the Nibe's local page via port forwarding is still to discuss.
- **Hardware still needed:** two resistors (1 kΩ + 2 kΩ) for flashing, female-female
  jumper wires. Have: a USB breakout with pins (check it has 5.1 kΩ CC resistors if
  it's USB-C).

## Gotchas to remember

- No auto-reset: entering the bootloader is manual (IO0→GND, then power-cycle).
- GPIO0 is both the boot pin and the 50 MHz Ethernet clock. Remove the IO0→GND jumper
  after flashing.
- Ethernet only starts if GPIO16 is driven high (it powers the oscillator).
- The dongle routes with NAT; it isn't a transparent bridge. Reaching the Nibe or
  Checkwatt *from* the home LAN needs port forwarding.
- The cable side is 10.10.0.0/24, on purpose far from the Deco's 192.168.68.0/22.
- Windows with Wi-Fi and the cable both up: `ping -S 10.10.0.2 ...` to force the cable.
  Targets on 192.168.68.x always go over Wi-Fi.
- ESPHome can't do the bridging (it runs Wi-Fi *or* Ethernet).
