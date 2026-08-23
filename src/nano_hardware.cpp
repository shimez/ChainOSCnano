#include "nano_hardware.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "config.h"

namespace {

Adafruit_NeoPixel rgbLed(RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

}  // namespace

void nanoHardwareSetColor(uint8_t red, uint8_t green, uint8_t blue) {
  if (!RGB_LED_ENABLED) return;
  rgbLed.setPixelColor(0, rgbLed.Color(red, green, blue));
  rgbLed.show();
}

void nanoHardwareSetup() {
  if (CHAIN_POWER_CONTROL_ENABLED) {
    pinMode(CHAIN_POWER_PIN, OUTPUT);
    digitalWrite(CHAIN_POWER_PIN, CHAIN_POWER_ACTIVE_LEVEL);
  }

  if (RGB_LED_ENABLED) {
    rgbLed.begin();
    rgbLed.setBrightness(RGB_LED_BRIGHTNESS);
    rgbLed.clear();
    rgbLed.show();
    nanoHardwareSetColor(0, 0, 255);
  }

  Serial.printf(
      "[ChainOSCnano][GPIO] chain_power=GPIO%u enabled=%s level=%s\n",
      CHAIN_POWER_PIN, CHAIN_POWER_CONTROL_ENABLED ? "true" : "false",
      CHAIN_POWER_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.printf("[ChainOSCnano][GPIO] rgb=GPIO%u enabled=%s\n", RGB_LED_PIN,
                RGB_LED_ENABLED ? "true" : "false");
}

