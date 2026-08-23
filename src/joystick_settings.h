#pragma once

#include <Arduino.h>

#include "key_settings.h"

struct JoystickSetting {
  String identity;
  String displayName;
  String xAddress = "/avatar/parameters/JoyX";
  String yAddress = "/avatar/parameters/JoyY";
  int deadband = 3;
  bool invertX = false;
  bool invertY = false;
  float outputMin = 0;
  float outputMax = 1;
  ValueType outputType = TYPE_FLOAT;
  KeyMode clickMode = MODE_PRESS_RELEASE;
  KeyOscMessage pressMessages[MAX_KEY_OSC_MESSAGES];
  KeyOscMessage releaseMessages[MAX_KEY_OSC_MESSAGES];
  uint8_t pressMessageCount = 1;
  uint8_t releaseMessageCount = 1;
  KeySequenceConfig clickSequence;
  uint8_t connectedPortMask = 0;
};

void joystickSettingsSetup();
JoystickSetting* joystickSettingsEnsure(const String& identity,
                                         const String& defaultName);
size_t joystickSettingsCount();
JoystickSetting* joystickSettingsAt(size_t index);
bool joystickSettingsSave(const JoystickSetting& candidate);
bool joystickSettingsDelete(const String& identity);
void joystickSettingsBeginPortUpdate(uint8_t portMask);
void joystickSettingsMarkConnected(const String& identity, uint8_t portMask);
