#pragma once

#include <Arduino.h>

void chainProbeSetup();
void chainProbeUpdate();
size_t chainProbeKeyCount();
String chainProbeKeyUid(size_t index);
bool chainProbeKeyConnected(const String& uid);
