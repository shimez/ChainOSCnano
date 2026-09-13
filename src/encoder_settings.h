#pragma once

#include <Arduino.h>

#include "key_settings.h"

enum EncoderSettingsModel : uint8_t {
  ENCODER_SETTINGS_LEGACY = 0,
  ENCODER_SETTINGS_V2 = 1
};

enum EncoderRotationMode : uint8_t {
  ENCODER_ROTATION_AMOUNT = 0,
  ENCODER_ROTATION_DIRECTION = 1
};

struct EncoderSetting {
  String identity;
  String displayName;
  EncoderSettingsModel settingsModel = ENCODER_SETTINGS_LEGACY;
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
  EncoderRotationMode rotationMode = ENCODER_ROTATION_AMOUNT;
  uint16_t rangeSteps = 20;
  bool clockwiseIncreases = true;
  String clockwiseValue = "0.05";
  String counterClockwiseValue = "-0.05";
  KeyMode pushMode = MODE_PRESS_RELEASE;
  int32_t logicalPosition = 0;
  bool logicalPositionInitialized = false;
  KeyMode clickMode = MODE_PRESS_RELEASE;
  KeyOscMessage pressMessages[MAX_KEY_OSC_MESSAGES];
  KeyOscMessage releaseMessages[MAX_KEY_OSC_MESSAGES];
  uint8_t pressMessageCount = 1;
  uint8_t releaseMessageCount = 1;
  KeySequenceConfig clickSequence;
  uint8_t connectedPortMask = 0;
};

bool encoderSettingsBuildV2MigrationCandidate(const EncoderSetting& legacy,
                                               EncoderSetting& candidate);
bool encoderSettingsCanLosslesslyMigrate(const EncoderSetting& legacy);

void encoderSettingsSetup();
EncoderSetting* encoderSettingsEnsure(const String& identity,
                                      const String& defaultName);
size_t encoderSettingsCount();
EncoderSetting* encoderSettingsAt(size_t index);
bool encoderSettingsSave(const EncoderSetting& candidate);
bool encoderSettingsDelete(const String& identity);
void encoderSettingsBeginPortUpdate(uint8_t portMask);
void encoderSettingsMarkConnected(const String& identity, uint8_t portMask);
