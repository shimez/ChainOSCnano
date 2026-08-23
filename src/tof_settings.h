#pragma once

#include <Arduino.h>

#include "key_settings.h"

struct TofSetting {
  String identity;
  String displayName;
  String address = "/avatar/parameters/ToF";
  int deadband = 5;
  int maxDistanceMm = 2000;
  bool nearValueHigh = false;
  float outputMin = 0;
  float outputMax = 1;
  ValueType outputType = TYPE_FLOAT;
  uint8_t connectedPortMask = 0;
};

void tofSettingsSetup();
TofSetting* tofSettingsEnsure(const String& identity, const String& defaultName);
size_t tofSettingsCount();
TofSetting* tofSettingsAt(size_t index);
bool tofSettingsSave(const TofSetting& candidate);
bool tofSettingsDelete(const String& identity);
void tofSettingsBeginPortUpdate(uint8_t portMask);
void tofSettingsMarkConnected(const String& identity, uint8_t portMask);
