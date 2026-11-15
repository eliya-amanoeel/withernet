# 1. Flash a WT32-ETH01 over serial

The WT32-ETH01 has no USB port, so the first flash goes through our Nano, used as a
USB-to-serial adapter. After that, firmware is updated over Wi-Fi from the dongle's status page
(`http://<management ip>/`, upload the signed `firmware-signed.bin` from
`tools/build-signed.sh`).
Repeat this for every new dongle, when a dongle can't be reached over Wi-Fi, or if the
signing key is lost (a wired flash accepts any build).

The Nano's own sketch (B) is built with PlatformIO. The ESP is flashed with ESP-IDF 5.3.1
on the Mac (D, E). Wired flashing used to be done on the PC with PlatformIO; the ESP-IDF
commands below haven't been tried with the Nano yet.

> **Our Nano** is a Nano-sized clone with an **ATmega32U4** (the big chip reads
> `MEGA32U4`). It does USB itself, so Windows lists it as **Arduino Leonardo**
> (USB ID `2341:8036`).

## A. Find the Nano's port

Plug the Nano in with a **data** cable: the USB-C ↔ USB-C one. Our USB-A → USB-C cable is
charge-only (power LED lights, but no port appears).

- **Windows:** Device Manager → Ports (COM & LPT) → `Arduino Leonardo (COMx)`.
- **macOS:** `ls /dev/cu.*` and look for `/dev/cu.usbmodemXXXX`. On Apple Silicon,
  accept the *"Allow accessory to connect?"* prompt.

`pio device list` shows the current port too. **The port can change** after an
upload (a 32U4 reports different USB IDs for its bootloader and for sketches), so check
again if an upload can't find it.

## B. Turn the Nano into a USB-serial adapter

The `tools/usb-serial-bridge` sketch passes bytes between USB and the Nano's `D0`/`D1`
pins, at whatever baud rate the computer opens the port with. Skip this if the Nano
already runs it.

In a **PlatformIO: New Terminal** (so `pio` is on the PATH; on the Mac,
`~/.platformio/penv/bin/pio`):

```sh
cd tools/usb-serial-bridge
pio run -t upload --upload-port COM6
```

Replace `COM6` with your port (on the Mac, `/dev/cu.usbmodemXXXX`).

**Optional loopback check:** unplug the Nano, jumper `D0` → `D1`, plug it in, run
`pio device monitor --port COM6` and type. Characters should come back as-is, and stop
when you pull the jumper. Remove the jumper afterwards.

## C. Wire the Nano to the ESP

Unplug the Nano first. The wiring is **crossed** (TX to RX):

| Wire | Nano | WT32-ETH01 |
|------|------|------------|
| data | `TX` (D1) → **divider** → | `RX0` |
| data | `RX` (D0) | `TX0` |
| power | `VCC` / `5V` | `5V` |
| ground | `GND` | `GND` (right column, above `5V`) |
| boot jumper | | `IO0` → `GND` (the pin just below `IO0`) |

- **Divider:** the Nano's `TX` is 5 V and the ESP's `RX0` is 3.3 V. Put 1 kΩ between
  Nano `TX` and `RX0`, and 2 kΩ from `RX0` to `GND`. The other wires go straight across.
- **Power:** use the Nano's `5V`, never its `3V3` (too weak for an ESP32). Don't connect
  the USB charger at the same time.
- **Boot jumper:** `IO0` → `GND` must be in place *before* the ESP powers up, or it boots
  the existing firmware instead of waiting for a new one.

## D. Flash

Set up ESP-IDF in the terminal first (see [handoff](../handoff.md#esp-idf-on-the-mac)):
`. ~/withernet/tools/idf-env.sh`. Then, from `firmware/bridge`, with the Nano's port
from step A.

**Erase first** on a board's first flash, or when `partitions.csv` changes (touch `EN` to
`GND` first):

```sh
idf.py -B build/checkwatt -DBOARD=checkwatt -p /dev/cu.usbmodemXXXX -b 115200 erase-flash
```

Each dongle has its own build (`BOARD=nibe`, `BOARD=checkwatt`) with its management IP;
without `BOARD` the build stops on purpose. Plug the Nano into the computer. The ESP
powers up in download mode. Then:

```sh
idf.py -B build/checkwatt -DBOARD=checkwatt -p /dev/cu.usbmodemXXXX -b 115200 flash
```

This writes the bootloader, partition table and firmware. A wired flash doesn't need a
signature; updates over Wi-Fi do.

**Speed:** flash at **460800 baud or lower** (`-b`). A 16 MHz AVR can't generate 921600
accurately. On the PC, only 115200 worked.

**No auto-reset:** `sdkconfig.defaults` sets `CONFIG_ESPTOOLPY_BEFORE_NORESET`. The Nano
drops data while DTR/RTS are low, and esptool's default reset leaves them low, so the
upload fails with `No serial data received`.

The bridge sketch has no auto-reset, so to flash again: unplug and replug the Nano (or
briefly touch `EN` to `GND`) with the boot jumper still in place.

## E. Run the new firmware

1. Unplug the Nano.
2. **Remove the `IO0` → `GND` jumper.** On the WT32-ETH01, `IO0` is also the 50 MHz
   Ethernet clock; with the jumper in, Ethernet won't work.
3. Power it up again (Nano or USB charger, not both). To watch the serial log, keep the
   Nano connected and run
   `idf.py -B build/checkwatt -p /dev/cu.usbmodemXXXX monitor --no-reset`.
