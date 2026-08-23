#pragma once

#include <Arduino.h>

#include "key_settings.h"

struct AngleSetting {
  String identity;
  String displayName;
  String address = "/avatar/parameters/Angle";
  bool use12Bit = true;
  int deadband = 8;
  float outputMin = 0;
  float outputMax = 1;
  ValueType outputType = TYPE_FLOAT;
  uint8_t connectedPortMask = 0;
};

void angleSettingsSetup();
AngleSetting* angleSettingsEnsure(const String& identity,
                                  const String& defaultName);
size_t angleSettingsCount();
AngleSetting* angleSettingsAt(size_t index);
bool angleSettingsSave(const AngleSetting& candidate);
bool angleSettingsDelete(const String& identity);
void angleSettingsBeginPortUpdate(uint8_t portMask);
void angleSettingsMarkConnected(const String& identity, uint8_t portMask);
