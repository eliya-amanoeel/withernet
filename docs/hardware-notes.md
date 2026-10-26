# Hardware notes & gotchas

## Nano as a USB-serial adapter: wire **straight**, not crossed

With the ATmega held in reset (RESET → GND), the Nano's `RX`/`TX` labels describe the
ATmega's side, so the USB chip's TX actually comes out on the pin marked **RX**.

| Nano | WT32-ETH01 |
|------|------------|
| `RX` (D0) | `RX0` |
| `TX` (D1) | `TX0` |
| `5V` | `5V` |
| `GND` | `GND` |
| `RESET` → `GND` | — |

**Logic level:** the Nano's D0 drives 5 V into the ESP32's 3.3 V `RX0`. It usually
survives, but it's out of spec. Add a divider on that one line (e.g. 1 kΩ in series,
2 kΩ to GND). The ESP's 3.3 V `TX0` is read fine by the Nano.

**Power:** use the Nano's `5V` pin. Don't use its `3V3` pin, which can't supply an
ESP32 with Ethernet running.

## Boot mode: GPIO0

Jumper `IO0` → `GND` *before* powering on to enter the bootloader. Remove it
afterwards: on the WT32-ETH01, GPIO0 is also the 50 MHz Ethernet clock input.

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

## Topology: NAT router, not a true bridge

An ESP32 in Wi-Fi client mode can't do a transparent layer-2 bridge, so the dongle routes
with NAT: the devices behind it get their own small subnet. Outbound cloud traffic (Nibe
myUplink, Checkwatt) works as normal. Anything that needs to reach those devices *from*
the home LAN (local web UI, Modbus) needs port forwarding on the dongle.
