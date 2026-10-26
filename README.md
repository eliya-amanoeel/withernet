# withernet

A DIY Wi-Fi → Ethernet dongle built on a **WT32-ETH01** (ESP32 + LAN8720), meant to replace
Powerline adapters for low-bandwidth gear (Nibe heat pump, Checkwatt gateway).

```
home router ))) 2.4 GHz Wi-Fi ))) WT32-ETH01 ──RJ-45──> Nibe / Checkwatt
```

## Goals

1. **Documentation**: how to wire, flash and install the thing, step by step.
2. **Painless updates**: flash over USB-serial *once*, then update every unit
   over Wi-Fi (OTA) straight from VS Code + PlatformIO.

## Status

| Step | What | Status |
|------|------|--------|
| 1 | [Check the Arduino Nano still works](docs/01-nano-check.md) | in progress |
| 2 | First serial flash of the WT32-ETH01 via the Nano | todo |
| 3 | Bridge firmware (Wi-Fi uplink → Ethernet downlink) | todo |
| 4 | OTA updates from VS Code | todo |

## Repo layout

```
docs/               guides, in the order you need them
tools/nano-check/   PlatformIO sketch to sanity-check the Nano
```

## Prerequisites

- VS Code with the **PlatformIO IDE** extension
- A USB data cable (not charge-only) for the Nano

See also: [hardware notes and gotchas](docs/hardware-notes.md).
