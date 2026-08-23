#pragma once

#include <Arduino.h>

#include "config.h"

enum ValueType : uint8_t { TYPE_FLOAT = 0, TYPE_INT = 1, TYPE_STRING = 2 };
enum KeyMode : uint8_t { MODE_PRESS_RELEASE = 0, MODE_SEQUENCE = 1 };

struct KeyOscMessage {
  String address;
  String value;
  ValueType type = TYPE_INT;
};

struct KeySequence {
  String address;
  ValueType type = TYPE_INT;
  float start = 0;
  float end = 1;
  float step = 1;
  float current = 0;
};

struct KeySetting {
  String uid;
  String name;
  KeyMode mode = MODE_PRESS_RELEASE;
  KeyOscMessage press[MAX_KEY_OSC_MESSAGES];
  KeyOscMessage release[MAX_KEY_OSC_MESSAGES];
  uint8_t pressCount = 1;
  uint8_t releaseCount = 1;
  KeySequence sequence;
  bool persisted = false;
};

void keySettingsSetup();
KeySetting* keySettingsEnsure(const String& uid);
KeySetting* keySettingsFind(const String& uid);
size_t keySettingsCount();
KeySetting* keySettingsAt(size_t index);
bool keySettingsSave(KeySetting& setting);
bool keySettingsDelete(const String& uid);
bool keySettingsValidAddress(const String& address);
bool keySettingsValidMessage(const KeyOscMessage& message);
void keySettingsNormalizeSequence(KeySequence& sequence);
