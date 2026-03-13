#include <Arduino.h>
#include "audio.h"

void setup() {
  Serial.begin(115200);

  initSinTable();

  setupI2S(SAMPLE_RATE, 2);
}

void loop() {
}


