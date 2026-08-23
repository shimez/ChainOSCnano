/*
 * Arduino IDE entry point for ChainOSCnano.
 *
 * Shared implementation lives in src/app.cpp. PlatformIO builds
 * src/main.cpp instead of this root sketch.
 */

#include "src/app.h"

void setup() {
  appSetup();
}

void loop() {
  appLoop();
}

