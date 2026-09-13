#pragma once

#include <Arduino.h>

void chainProbeSetup();
void chainProbeUpdate();
bool chainProbeIdentifyDevice(const String& identity);
void chainProbeResetAngleRuntimeState(const String& identity);
size_t chainProbeConnectedDeviceCount();
bool chainProbeConnectedDeviceAt(size_t index, String& identity,
                                uint8_t& deviceType);
