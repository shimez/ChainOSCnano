#pragma once

#include <Arduino.h>

#include "key_settings.h"

struct EncoderSetting {
  String identity;
  String displayName;
  String rotationAddress = "/avatar/parameters/Encoder";
  bool sendIncrement = false;
  bool wrapAround = true;
  float boundedAbsolute = 0;
  bool boundedAbsoluteInitialized = false;
  float absoluteInputMin = 0;
  float absoluteInputMax = 20;
  float incrementScale = 0.05f;
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

void encoderSettingsSetup();
EncoderSetting* encoderSettingsEnsure(const String& identity,
                                      const String& defaultName);
size_t encoderSettingsCount();
EncoderSetting* encoderSettingsAt(size_t index);
bool encoderSettingsSave(const EncoderSetting& candidate);
bool encoderSettingsDelete(const String& identity);
void encoderSettingsBeginPortUpdate(uint8_t portMask);
void encoderSettingsMarkConnected(const String& identity, uint8_t portMask);
