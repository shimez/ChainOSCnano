#include "app.h"

// Arduino IDE builds ChainOSCnano.ino. PlatformIO builds this file.
#ifdef CHAINOSCNANO_PLATFORMIO
void setup() {
  appSetup();
}

void loop() {
  appLoop();
}
#endif

