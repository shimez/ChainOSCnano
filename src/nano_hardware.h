#pragma once

#include <Arduino.h>

enum class NetworkLedState {
  CONNECTING,
  CONNECTED,
  AP_MODE,
};

void nanoHardwareSetup();
void nanoHardwareUpdate();
void nanoHardwareSetColor(uint8_t red, uint8_t green, uint8_t blue);
void nanoSetNetworkLedState(NetworkLedState state);
bool nanoIdentifyDevice(const String& identity);
