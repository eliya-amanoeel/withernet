# 1. Check the Arduino Nano

The Nano will later act as a USB-to-serial adapter for the first WT32-ETH01 flash.
Before relying on it, confirm three things: the computer sees it, it accepts an upload,
and serial works in both directions.

> **Which Nano?** Ours is a Nano-sized clone with an **ATmega32U4** (read the big chip:
> `MEGA32U4`). The 32U4 does USB itself, so there's no CH340 chip and Windows calls it
> **Arduino Leonardo**. A classic Nano has an `ATMEGA328P` plus a CH340 and shows up as
> `USB-SERIAL CH340`. This guide covers the 32U4; notes for a classic Nano are at the end.

## A. Does the computer see it?

Plug the Nano in with a **data** cable. The power LED should light.

**Windows:** open **Device Manager → Ports (COM & LPT)**. You should see
`Arduino Leonardo (COMx)`; write down the COM number.

**macOS:** run `ls /dev/cu.*` and look for a new `/dev/cu.usbmodemXXXX` entry. On Apple
Silicon Macs, accept the *"Allow accessory to connect?"* prompt.

- **The power LED lights but no port appears.** The cable carries power but not data.
  Our USB-A → USB-C cable did exactly this; a USB-C ↔ USB-C cable worked. To test a
  cable, plug a phone in with it: if the phone only charges and never offers file
  transfer, the cable is charge-only.
- **An entry exists but its properties say Code 45 ("not connected").** That's a leftover
  from an earlier connection, not the board you just plugged in.
- **The power LED doesn't light.** The cable or port isn't supplying power. Try another
  one before blaming the board.

## B. Upload the test sketch

In VS Code: open `tools/nano-check` as a PlatformIO project, then click **Upload**
(→ in the bottom bar). From a terminal instead (use **PlatformIO: New Terminal** from the
command palette so `pio` is on the PATH):

```sh
cd tools/nano-check
pio run -t upload --upload-port COM6
```

Replace `COM6` with your port. Without `--upload-port`, PlatformIO may fail with
*"Please specify upload_port"*.

**The COM number can change** after an upload: Windows numbers ports per USB ID, and a
32U4 reports a different ID for the bootloader, the shipped firmware and our sketches.
Ours went from COM4 to COM6. `pio device list` shows the current one.

✅ **Pass:** the upload succeeds and the onboard LED blinks fast (4×/s).

## C. Test serial in both directions

```sh
pio device monitor --port COM6
```

You should see `withernet nano-check: alive...`. Type a few characters. Each one should
come back as `echo: x`. Quit with `Ctrl+C`.

✅ **Pass:** both the banner and the echo work.

## D. Test the USB-serial bridge (how we'll actually use it)

On a 32U4 the classic trick of holding the ATmega in reset doesn't work: USB lives
inside the chip and would switch off too. Instead, the `tools/usb-serial-bridge` sketch
passes bytes between USB and the D0/D1 pins, following whatever baud rate the computer
opens the port with.

1. Upload the bridge:
   ```sh
   cd ../usb-serial-bridge
   pio run -t upload --upload-port COM6
   ```
   The LED stops blinking; the bridge doesn't use it.
2. Unplug the Nano and jumper **D0 (RX) → D1 (TX)** (a loopback).
3. Plug it in, run `pio device monitor --port COM6` and type characters. They should come
   back as-is (no `echo:` prefix), echoed by the wire rather than by a sketch. Pull the
   jumper and they should stop coming back.

✅ **Pass:** the characters come back. Remove the jumper, and the Nano is ready to flash the
WT32-ETH01.

**Speed:** in our test, 4 KB of random data looped back intact at 115200, 230400 and
460800 baud, but not at 921600 (a 16 MHz AVR can't generate that rate accurately).
Flash at **460800 or lower**.

If A–D all pass, mark step 1 as done in the README.

## Classic Nano (ATmega328P + CH340)

- **A:** it shows up as `USB-SERIAL CH340 (COMx)`. If it's under *Other devices* with a
  warning icon instead, install the WCH driver (`CH341SER.EXE`) and replug.
- **B:** use `pio run -e nano_old -t upload`; if that fails with
  `stk500_getsync(): not in sync`, try `-e nano_new` (newer bootloader).
- **D:** no bridge sketch needed. Jumper **RESET → GND** to silence the ATmega, and the
  CH340 talks straight to D0/D1. Note this flips the wiring to the ESP32, see the
  [hardware notes](hardware-notes.md).
