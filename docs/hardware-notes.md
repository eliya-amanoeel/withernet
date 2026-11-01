# Hardware notes & gotchas

Wiring for flashing is in the [flashing guide](01-flashing.md).

## Power

| When | Source | Wiring |
|------|--------|--------|
| Flashing | PC USB → Nano | Nano `5V` → ESP `5V`, `GND` → `GND` |
| Running | USB phone charger (5 V, 1 A+) → USB breakout | breakout `5V`/`VBUS` → ESP `5V`, `GND` → `GND` (female-female jumpers, no soldering) |

- Never connect both sources at once.
- Never put 5 V on a `3V3` pin. No reverse-polarity protection, so don't swap 5V/GND.
- A USB-C breakout needs 5.1 kΩ CC resistors on board, or a USB-C charger won't
  supply power.

## GPIO0: boot pin and Ethernet clock

`IO0` → `GND` at power-up enters the bootloader. The same pin is the 50 MHz Ethernet clock
input, so remove the jumper after flashing.

## WT32-ETH01 Ethernet pins

| Signal | Pin |
|--------|-----|
| MDC | GPIO23 |
| MDIO | GPIO18 |
| Clock in (50 MHz) | GPIO0 |
| Oscillator enable / PHY power | GPIO16 |
| PHY address | 1 |
| PHY | LAN8720 |

Ethernet won't start unless GPIO16 is driven high.

## Topology: MAC-cloning bridge

A Wi-Fi client (station) may only send frames from its own MAC, so an ESP32 can't be a
plain layer-2 bridge for other devices. Powerline gets around this because both adapters
speak the same protocol; with Wi-Fi the other end is the Deco, which follows the normal
client rules.

The dongle sidesteps that by **cloning the device's MAC**:

1. Ethernet comes up; the dongle waits for the first frame from the device and reads
   its source MAC.
2. It sets that MAC on its Wi-Fi station interface and joins the Deco.
3. It forwards every frame unchanged: Ethernet → Wi-Fi and Wi-Fi → Ethernet.

To the Deco, the Nibe/Checkwatt is simply a Wi-Fi client. It gets its IP from the Deco,
shows up in the Deco app under its real MAC, and is reachable from the home LAN like any
other device. No NAT and no port forwarding.

Limits:

- **One device per dongle.** Frames from any other MAC are dropped.
- **No address of its own.** The dongle borrows the device's identity, so OTA and a
  status page need a separate management IP (planned for later).
- **2.4 GHz only**, roughly 5–20 Mbps.
- Uses undocumented ESP-IDF calls; pin the IDF version once it works.
