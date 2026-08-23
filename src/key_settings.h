#pragma once

#include <Arduino.h>

constexpr uint8_t MAX_KEY_OSC_MESSAGES = 8;

enum ValueType : uint8_t { TYPE_FLOAT = 0, TYPE_INT = 1, TYPE_STRING = 2 };
enum KeyMode : uint8_t { MODE_PRESS_RELEASE = 0, MODE_SEQUENCE = 1 };

struct KeyOscMessage {
  String address;
  String valueStr;
  ValueType valueType = TYPE_INT;
};

struct KeySequenceConfig {
  String address;
  ValueType valueType = TYPE_FLOAT;
  float start = 0;
  float end = 10;
  float step = 1;
  float current = 0;
};

struct KeySetting {
  String identity;
  String displayName;
  KeyMode mode = MODE_PRESS_RELEASE;
  KeyOscMessage pressMessages[MAX_KEY_OSC_MESSAGES];
  KeyOscMessage releaseMessages[MAX_KEY_OSC_MESSAGES];
  uint8_t pressMessageCount = 1;
  uint8_t releaseMessageCount = 1;
  KeySequenceConfig sequence;
  bool builtIn = false;
  uint8_t connectedPortMask = 0;
};

void keySettingsSetup();
KeySetting* keySettingsEnsure(const String& identity, const String& defaultName,
                              const String& defaultAddress);
size_t keySettingsCount();
KeySetting* keySettingsAt(size_t index);
bool keySettingsSave(const KeySetting& candidate);
bool keySettingsDelete(const String& identity);
void keySettingsNormalizeSequence(KeySequenceConfig& sequence);
void keySettingsBeginPortUpdate(uint8_t portMask);
void keySettingsMarkConnected(const String& identity, uint8_t portMask);
void keySettingsPrintState();
