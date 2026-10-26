# 1. Check the Arduino Nano

The Nano will later act as a plain USB-to-serial adapter for the first WT32-ETH01 flash.
Before relying on it, confirm three things: the USB chip shows up, the ATmega accepts
an upload, and serial works in both directions.

## A. Does the computer see it?

Plug the Nano in with a **data** cable. The power LED should light.

**Windows:** open **Device Manager → Ports (COM & LPT)**. You should see an entry such as
`USB-SERIAL CH340 (COM3)`; write down the COM number. If it appears under *Other devices*
with a yellow warning icon instead, the CH340 driver is missing. Install it from WCH
(`CH341SER.EXE`), then unplug and replug the Nano.

**macOS:** run `ls /dev/cu.*` and look for a new `/dev/cu.usbserial-XXXX` entry. On Apple
Silicon Macs, accept the *"Allow accessory to connect?"* prompt.

- **Nothing new appears.** Try another cable first (charge-only cables are the most
  common cause), then another USB port.
- **The power LED doesn't light.** The cable or port isn't supplying power. Try another
  one before blaming the board.

## B. Upload the test sketch

In VS Code: open `tools/nano-check` as a PlatformIO project, then click **Upload**
(→ in the bottom bar). From a terminal instead (use **PlatformIO: New Terminal** from the
command palette so `pio` is on the PATH):

```sh
cd tools/nano-check
pio run -t upload
```

Most clones use the **old bootloader**, which is the default env (`nano_old`). If the
upload fails with `stk500_getsync(): not in sync`, try the new bootloader:

```sh
pio run -e nano_new -t upload
```

✅ **Pass:** the upload succeeds and the onboard LED blinks fast (4×/s).

## C. Test serial in both directions

```sh
pio device monitor
```

You should see `withernet nano-check: alive...`. Type a few characters. Each one should
come back as `echo: x`.

✅ **Pass:** both the banner and the echo work.

## D. Test pass-through mode (how we'll actually use it)

This is the exact mode used to flash the ESP32: the ATmega is held in reset, and the USB
chip talks straight to the D0/D1 pins.

1. Unplug the Nano.
2. Jumper **RESET → GND** (the ATmega is now silent; the LED stops blinking).
3. Jumper **D0 (RX) → D1 (TX)** (a loopback).
4. Plug it in and run `pio device monitor`.
5. Type characters. They should appear on screen, echoed by the loopback wire rather
   than by the sketch (no `echo:` prefix).

✅ **Pass:** the characters come back. Remove the D0–D1 jumper, and the Nano is ready to
flash the WT32-ETH01.

If A–D all pass, mark step 1 as done in the README.
