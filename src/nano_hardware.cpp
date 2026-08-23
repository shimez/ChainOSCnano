#include "nano_hardware.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "config.h"

namespace {

Adafruit_NeoPixel rgbLed(RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
NetworkLedState networkLedState = NetworkLedState::CONNECTING;
unsigned long lastBlinkMs = 0;
bool blinkOn = true;

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

void nanoSetNetworkLedState(NetworkLedState state) {
  networkLedState = state;
  switch (state) {
    case NetworkLedState::CONNECTED:
      nanoHardwareSetColor(0, 255, 255);
      break;
    case NetworkLedState::AP_MODE:
      nanoHardwareSetColor(255, 0, 0);
      break;
    case NetworkLedState::CONNECTING:
    default:
      blinkOn = true;
      lastBlinkMs = millis();
      nanoHardwareSetColor(0, 0, 255);
      break;
  }
}

void nanoHardwareUpdate() {
  if (networkLedState != NetworkLedState::CONNECTING) return;
  const unsigned long now = millis();
  if (now - lastBlinkMs < 500) return;
  lastBlinkMs = now;
  blinkOn = !blinkOn;
  nanoHardwareSetColor(0, 0, blinkOn ? 255 : 0);
}

bool nanoIdentifyDevice(const String&) {
  // The NanoC6 itself is not represented by a device settings card.
  return false;
}
