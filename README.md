# withernet

A DIY Wi-Fi → Ethernet dongle built on a **WT32-ETH01** (ESP32 + LAN8720), meant to replace
Powerline adapters for low-bandwidth gear (Nibe heat pump, Checkwatt gateway).

```
home router ))) 2.4 GHz Wi-Fi ))) WT32-ETH01 ──RJ-45──> Nibe / Checkwatt
```

## Goals

1. **Documentation**: how to wire, flash and install the thing, step by step.
2. **Painless updates**: for now over USB-serial (via the Nano). Updates over Wi-Fi
   (OTA) are a future feature.

## Status

> Picking this up again? Start with [HANDOFF.md](HANDOFF.md).

| Step | What | Status |
|------|------|--------|
| 1 | [Check the Arduino Nano still works](docs/01-nano-check.md) | done |
| 2 | First serial flash of the WT32-ETH01 via the Nano | done |
| 3 | Router firmware (Wi-Fi uplink → Ethernet downlink, NAT) | in progress |
| 4 | OTA updates from VS Code | future (updates go over the wire for now) |

## Repo layout

```
docs/               guides, in the order you need them
tools/nano-check/          PlatformIO sketch to sanity-check the Nano
tools/usb-serial-bridge/   turns the Nano into a USB-serial adapter for flashing
firmware/                  the dongle's own firmware (PlatformIO, run pio from inside it)
```

## Prerequisites

- VS Code with the **PlatformIO IDE** extension
- A USB data cable (not charge-only) for the Nano. Our USB-A → USB-C cable turned out
  to be charge-only; a USB-C ↔ USB-C cable works.

See also: [hardware notes and gotchas](docs/hardware-notes.md).
