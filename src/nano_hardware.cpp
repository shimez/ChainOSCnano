#include "nano_hardware.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "config.h"
#include "osc_manager.h"

namespace {

Adafruit_NeoPixel rgbLed(RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
NetworkLedState networkLedState = NetworkLedState::CONNECTING;
unsigned long lastBlinkMs = 0;
bool blinkOn = true;
bool rawButtonPressed = false;
bool stableButtonPressed = false;
unsigned long buttonChangedAtMs = 0;
unsigned long identifyUntilMs = 0;

bool identifyActive(unsigned long now) {
  return identifyUntilMs != 0 &&
         static_cast<long>(identifyUntilMs - now) > 0;
}

void renderLed(unsigned long now) {
  if (identifyActive(now) || stableButtonPressed) {
    nanoHardwareSetColor(255, 64, 0);
    return;
  }
  switch (networkLedState) {
    case NetworkLedState::CONNECTED:
      nanoHardwareSetColor(0, 255, 255);
      break;
    case NetworkLedState::AP_MODE:
      nanoHardwareSetColor(255, 0, 0);
      break;
    case NetworkLedState::CONNECTING:
    default:
      nanoHardwareSetColor(0, 0, blinkOn ? 255 : 0);
      break;
  }
}

}  // namespace

void nanoHardwareSetColor(uint8_t red, uint8_t green, uint8_t blue) {
  if (!RGB_LED_ENABLED) return;
  rgbLed.setPixelColor(0, rgbLed.Color(red, green, blue));
  rgbLed.show();
}

void nanoHardwareSetup() {
  pinMode(BUILT_IN_BUTTON_PIN, INPUT_PULLUP);
  const bool pressed = digitalRead(BUILT_IN_BUTTON_PIN) == LOW;
  rawButtonPressed = pressed;
  stableButtonPressed = pressed;
  buttonChangedAtMs = millis();
  if (CHAIN_POWER_CONTROL_ENABLED) {
    pinMode(CHAIN_POWER_PIN, OUTPUT);
    digitalWrite(CHAIN_POWER_PIN, CHAIN_POWER_ACTIVE_LEVEL);
  }

  if (RGB_LED_ENABLED) {
    rgbLed.begin();
    rgbLed.setBrightness(RGB_LED_BRIGHTNESS);
    rgbLed.clear();
    rgbLed.show();
    renderLed(millis());
  }

  Serial.printf(
      "[ChainOSCnano][GPIO] chain_power=GPIO%u enabled=%s level=%s\n",
      CHAIN_POWER_PIN, CHAIN_POWER_CONTROL_ENABLED ? "true" : "false",
      CHAIN_POWER_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.printf("[ChainOSCnano][GPIO] rgb=GPIO%u enabled=%s\n", RGB_LED_PIN,
                RGB_LED_ENABLED ? "true" : "false");
  Serial.printf("[ChainOSCnano][GPIO] button=GPIO%u debounce=%lums\n",
                BUILT_IN_BUTTON_PIN, BUTTON_DEBOUNCE_MS);
}

void nanoSetNetworkLedState(NetworkLedState state) {
  networkLedState = state;
  if (state == NetworkLedState::CONNECTING) {
    blinkOn = true;
    lastBlinkMs = millis();
  }
  renderLed(millis());
}

void nanoHardwareUpdate() {
  const unsigned long now = millis();
  const bool pressed = digitalRead(BUILT_IN_BUTTON_PIN) == LOW;
  if (pressed != rawButtonPressed) {
    rawButtonPressed = pressed;
    buttonChangedAtMs = now;
  }
  if (stableButtonPressed != rawButtonPressed &&
      now - buttonChangedAtMs >= BUTTON_DEBOUNCE_MS) {
    stableButtonPressed = rawButtonPressed;
    renderLed(now);
    Serial.printf("[ChainOSCnano][BUTTON] state=%s uptime=%lu ms\n",
                  stableButtonPressed ? "PRESSED" : "RELEASED", now);
    oscSendNanoButton(stableButtonPressed);
  }
  if (identifyUntilMs != 0 && !identifyActive(now)) {
    identifyUntilMs = 0;
    renderLed(now);
  }
  if (networkLedState == NetworkLedState::CONNECTING &&
      now - lastBlinkMs >= 500) {
    lastBlinkMs = now;
    blinkOn = !blinkOn;
    renderLed(now);
  }
}

bool nanoIdentifyDevice(const String& identity) {
  if (identity != "nano:button" || !RGB_LED_ENABLED) return false;
  identifyUntilMs = millis() + 10000UL;
  renderLed(millis());
  return true;
}
