// Blinks the onboard LED and echoes anything typed in the serial monitor.
// If both work, the USB chip, the ATmega and the TX/RX path are all alive.
#include <Arduino.h>

unsigned long lastBlink = 0;
bool ledOn = false;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println(F("withernet nano-check: alive. Type something, it will be echoed."));
}

void loop() {
  if (millis() - lastBlink >= 250) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn);
  }

  while (Serial.available()) {
    char c = Serial.read();
    Serial.print(F("echo: "));
    Serial.println(c);
  }
}
