// Forwards bytes between USB (Serial) and the hardware UART on D0/D1 (Serial1).
// The UART follows whatever baud rate the computer opens the port with, so
// tools like esptool work without any settings here.
#include <Arduino.h>

uint32_t currentBaud = 0;
uint8_t buf[64];

void setup() {
  Serial.begin(115200);
}

void loop() {
  uint32_t baud = Serial.baud();
  if (baud != 0 && baud != currentBaud) {
    currentBaud = baud;
    Serial1.end();
    Serial1.begin(baud);
  }

  int n = Serial.available();
  if (n > 0) {
    n = Serial.readBytes(buf, min(n, (int)sizeof(buf)));
    Serial1.write(buf, n);
  }

  n = Serial1.available();
  if (n > 0) {
    n = Serial1.readBytes(buf, min(n, (int)sizeof(buf)));
    Serial.write(buf, n);
  }
}
