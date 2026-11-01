# withernet

A DIY Wi-Fi → Ethernet dongle built on a **WT32-ETH01** (ESP32 + LAN8720), meant to replace
the Powerline adapters for low-bandwidth gear (Nibe heat pump, Checkwatt gateway).

```
Deco XE75 ))) 2.4 GHz Wi-Fi ))) WT32-ETH01 ──RJ-45──> Nibe        (dongle 1)
Deco XE75 ))) 2.4 GHz Wi-Fi ))) WT32-ETH01 ──RJ-45──> Checkwatt   (dongle 2)
```

## Why

The Powerline sender sits on an outlet outside the 10" rack, cabled to the UniFi switch,
because the rack is full. With Wi-Fi the Deco is the sender, so that adapter and its cable
go away. Only one dongle per device is needed, at the device end.

## How it works: MAC cloning

The dongle is a transparent passthrough: the Deco sees the Nibe/Checkwatt itself, with
its own MAC, and assigns its IP over DHCP. No NAT, no separate subnet, no port forwarding.

A Wi-Fi client may only send frames from its own MAC, so a plain layer-2 bridge isn't
possible. Instead the dongle:

1. learns the MAC of the one device on its Ethernet port,
2. joins the Wi-Fi *using that MAC*,
3. forwards every frame unchanged, both ways.

Limits: **one device per dongle** (no switch behind it), 2.4 GHz only, roughly 5–20 Mbps.
Plenty for a heat pump and an energy gateway. See [hardware notes](docs/hardware-notes.md#topology-mac-cloning-bridge).

## Goals

1. **Documentation**: how to wire, flash and install the thing, step by step.
2. **Transparent bridge**: the router assigns the device's IP, as with Powerline.
3. **Painless updates** (later): flash over USB-serial once, then update over Wi-Fi (OTA).

## Status

> Picking this up again? Start with [handoff.md](handoff.md).

| Step | What | Status |
|------|------|--------|
| 1 | Check the Arduino Nano still works | done |
| 2 | [First serial flash of the WT32-ETH01 via the Nano](docs/01-flashing.md) | done |
| 3 | Minimal bridge firmware (MAC cloning), bench test with a laptop | todo |
| 4 | Install at the Nibe and Checkwatt, run for a week | todo |
| 5 | Extras: management IP, OTA updates, setup hotspot | todo |

## Repo layout

```
docs/                      guides, in the order you need them
firmware/bridge/           the bridge firmware (step 3, not written yet)
tools/usb-serial-bridge/   turns the Nano into a USB-serial adapter for flashing
```

## Prerequisites

- VS Code with the **PlatformIO IDE** extension
- A USB data cable (not charge-only) for the Nano. Our USB-A → USB-C cable turned out
  to be charge-only; a USB-C ↔ USB-C cable works.

See also: [hardware notes and gotchas](docs/hardware-notes.md).
