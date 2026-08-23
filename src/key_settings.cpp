#include "key_settings.h"

#include <Preferences.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

namespace {

KeySetting settings[MAX_SAVED_KEY_SETTINGS];
size_t settingCount = 0;

uint32_t fnv1a(const String& text) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index < text.length(); ++index) {
    hash ^= static_cast<uint8_t>(text[index]);
    hash *= 16777619u;
  }
  return hash;
}

String settingNamespace(const String& uid) {
  char name[11];
  snprintf(name, sizeof(name), "k%08lX", static_cast<unsigned long>(fnv1a(uid)));
  return String(name);
}

String messageKey(const char* prefix, uint8_t index) {
  return String(prefix) + String(index);
}

String defaultAddress(const String& uid) {
  return String(F("/chainoscnano/key/")) + uid;
}

void setDefaults(KeySetting& setting, const String& uid) {
  setting = KeySetting();
  setting.uid = uid;
  setting.name = String(F("Key ")) + uid.substring(uid.length() > 6 ? uid.length() - 6 : 0);
  const String address = defaultAddress(uid);
  setting.press[0].address = address;
  setting.press[0].value = "1";
  setting.press[0].type = TYPE_INT;
  setting.release[0].address = address;
  setting.release[0].value = "0";
  setting.release[0].type = TYPE_INT;
  setting.sequence.address = address;
}

bool loadSetting(const String& uid, KeySetting& setting) {
  Preferences preferences;
  const String ns = settingNamespace(uid);
  if (!preferences.begin(ns.c_str(), true)) return false;
  if (preferences.getString("uid", "") != uid) {
    preferences.end();
    return false;
  }
  setDefaults(setting, uid);
  setting.name = preferences.getString("name", setting.name);
  setting.mode = static_cast<KeyMode>(constrain(preferences.getUChar("mode", 0), 0, 1));
  setting.pressCount = constrain(preferences.getUChar("pc", 1), 0, MAX_KEY_OSC_MESSAGES);
  setting.releaseCount = constrain(preferences.getUChar("rc", 1), 0, MAX_KEY_OSC_MESSAGES - setting.pressCount);
  for (uint8_t index = 0; index < setting.pressCount; ++index) {
    setting.press[index].address = preferences.getString(messageKey("pa", index).c_str(), "");
    setting.press[index].value = preferences.getString(messageKey("pv", index).c_str(), "");
    setting.press[index].type = static_cast<ValueType>(constrain(preferences.getUChar(messageKey("pt", index).c_str(), TYPE_INT), 0, 2));
  }
  for (uint8_t index = 0; index < setting.releaseCount; ++index) {
    setting.release[index].address = preferences.getString(messageKey("ra", index).c_str(), "");
    setting.release[index].value = preferences.getString(messageKey("rv", index).c_str(), "");
    setting.release[index].type = static_cast<ValueType>(constrain(preferences.getUChar(messageKey("rt", index).c_str(), TYPE_INT), 0, 2));
  }
  setting.sequence.address = preferences.getString("sa", defaultAddress(uid));
  setting.sequence.type = static_cast<ValueType>(constrain(preferences.getUChar("st", TYPE_INT), 0, 2));
  setting.sequence.start = preferences.getFloat("ss", 0);
  setting.sequence.end = preferences.getFloat("se", 1);
  setting.sequence.step = preferences.getFloat("sp", 1);
  preferences.end();
  keySettingsNormalizeSequence(setting.sequence);
  setting.persisted = true;
  return true;
}

bool saveKnownList() {
  String list;
  for (size_t index = 0; index < settingCount; ++index) {
    if (!settings[index].persisted) continue;
    if (!list.isEmpty()) list += ',';
    list += settings[index].uid;
  }
  Preferences preferences;
  if (!preferences.begin(KEY_INDEX_NAMESPACE, false)) return false;
  const size_t written = preferences.putString("uids", list);
  preferences.end();
  return list.isEmpty() || written > 0;
}

bool sameMessage(const KeyOscMessage& left, const KeyOscMessage& right) {
  return left.address == right.address && left.value == right.value &&
         left.type == right.type;
}

bool sameSetting(const KeySetting& left, const KeySetting& right) {
  if (left.uid != right.uid || left.name != right.name ||
      left.mode != right.mode || left.pressCount != right.pressCount ||
      left.releaseCount != right.releaseCount ||
      left.sequence.address != right.sequence.address ||
      left.sequence.type != right.sequence.type ||
      fabsf(left.sequence.start - right.sequence.start) > 0.00001f ||
      fabsf(left.sequence.end - right.sequence.end) > 0.00001f ||
      fabsf(left.sequence.step - right.sequence.step) > 0.00001f)
    return false;
  for (uint8_t index = 0; index < left.pressCount; ++index)
    if (!sameMessage(left.press[index], right.press[index])) return false;
  for (uint8_t index = 0; index < left.releaseCount; ++index)
    if (!sameMessage(left.release[index], right.release[index])) return false;
  return true;
}

}  // namespace

bool keySettingsValidAddress(const String& address) {
  if (address.length() < 1 || address.length() > OSC_ADDRESS_MAX_BYTES || address[0] != '/') return false;
  for (size_t index = 0; index < address.length(); ++index) {
    const unsigned char value = static_cast<unsigned char>(address[index]);
    if (value < 0x20 || value == 0x7F) return false;
  }
  return true;
}

bool keySettingsValidMessage(const KeyOscMessage& message) {
  if (!keySettingsValidAddress(message.address) || message.value.length() > OSC_VALUE_MAX_BYTES) return false;
  if (message.type == TYPE_STRING) return true;
  if (message.type == TYPE_INT) {
    char* end = nullptr;
    errno = 0;
    const long value = strtol(message.value.c_str(), &end, 10);
    return end != message.value.c_str() && *end == '\0' && errno != ERANGE &&
           value >= INT32_MIN && value <= INT32_MAX;
  }
  char* end = nullptr;
  const float value = strtof(message.value.c_str(), &end);
  return end != message.value.c_str() && *end == '\0' && isfinite(value);
}

void keySettingsNormalizeSequence(KeySequence& sequence) {
  if (!isfinite(sequence.start)) sequence.start = 0;
  if (!isfinite(sequence.end)) sequence.end = 1;
  if (!isfinite(sequence.step) || fabsf(sequence.step) < 0.000001f) sequence.step = sequence.end >= sequence.start ? 1 : -1;
  if (sequence.start < sequence.end && sequence.step < 0) sequence.step = -sequence.step;
  if (sequence.start > sequence.end && sequence.step > 0) sequence.step = -sequence.step;
  sequence.current = sequence.start;
}

void keySettingsSetup() {
  Preferences preferences;
  String list;
  if (preferences.begin(KEY_INDEX_NAMESPACE, true)) {
    list = preferences.getString("uids", "");
    preferences.end();
  }
  size_t offset = 0;
  while (offset < list.length() && settingCount < MAX_SAVED_KEY_SETTINGS) {
    int separator = list.indexOf(',', offset);
    if (separator < 0) separator = list.length();
    const String uid = list.substring(offset, separator);
    if (!uid.isEmpty() && loadSetting(uid, settings[settingCount])) ++settingCount;
    offset = separator + 1;
  }
  Serial.printf("[ChainOSCnano][KEYCFG] loaded=%u\n", static_cast<unsigned>(settingCount));
}

KeySetting* keySettingsFind(const String& uid) {
  for (size_t index = 0; index < settingCount; ++index) if (settings[index].uid == uid) return &settings[index];
  return nullptr;
}

KeySetting* keySettingsEnsure(const String& uid) {
  if (KeySetting* existing = keySettingsFind(uid)) return existing;
  if (settingCount >= MAX_SAVED_KEY_SETTINGS) return nullptr;
  KeySetting& setting = settings[settingCount++];
  if (!loadSetting(uid, setting)) setDefaults(setting, uid);
  return &setting;
}

size_t keySettingsCount() { return settingCount; }
KeySetting* keySettingsAt(size_t index) { return index < settingCount ? &settings[index] : nullptr; }

bool keySettingsSave(KeySetting& setting) {
  if (setting.name.length() > DEVICE_NAME_MAX_BYTES ||
      setting.pressCount + setting.releaseCount > MAX_KEY_OSC_MESSAGES ||
      !keySettingsValidAddress(setting.sequence.address)) return false;
  for (uint8_t index = 0; index < setting.pressCount; ++index) if (!keySettingsValidMessage(setting.press[index])) return false;
  for (uint8_t index = 0; index < setting.releaseCount; ++index) if (!keySettingsValidMessage(setting.release[index])) return false;
  keySettingsNormalizeSequence(setting.sequence);
  Preferences preferences;
  const String ns = settingNamespace(setting.uid);
  if (!preferences.begin(ns.c_str(), false)) return false;
  preferences.clear();
  preferences.putString("uid", setting.uid);
  preferences.putString("name", setting.name);
  preferences.putUChar("mode", setting.mode);
  preferences.putUChar("pc", setting.pressCount);
  preferences.putUChar("rc", setting.releaseCount);
  for (uint8_t index = 0; index < setting.pressCount; ++index) {
    preferences.putString(messageKey("pa", index).c_str(), setting.press[index].address);
    preferences.putString(messageKey("pv", index).c_str(), setting.press[index].value);
    preferences.putUChar(messageKey("pt", index).c_str(), setting.press[index].type);
  }
  for (uint8_t index = 0; index < setting.releaseCount; ++index) {
    preferences.putString(messageKey("ra", index).c_str(), setting.release[index].address);
    preferences.putString(messageKey("rv", index).c_str(), setting.release[index].value);
    preferences.putUChar(messageKey("rt", index).c_str(), setting.release[index].type);
  }
  preferences.putString("sa", setting.sequence.address);
  preferences.putUChar("st", setting.sequence.type);
  preferences.putFloat("ss", setting.sequence.start);
  preferences.putFloat("se", setting.sequence.end);
  preferences.putFloat("sp", setting.sequence.step);
  preferences.end();
  KeySetting verified;
  if (!loadSetting(setting.uid, verified) || !sameSetting(setting, verified)) {
    Serial.printf("[ChainOSCnano][KEYCFG] save_verify_failed uid=%s\n",
                  setting.uid.c_str());
    return false;
  }
  setting.persisted = true;
  const bool indexed = saveKnownList();
  Serial.printf("[ChainOSCnano][KEYCFG] saved uid=%s mode=%u press=%u release=%u indexed=%s\n", setting.uid.c_str(), setting.mode, setting.pressCount, setting.releaseCount, indexed ? "true" : "false");
  return indexed;
}

bool keySettingsDelete(const String& uid) {
  size_t found = settingCount;
  for (size_t index = 0; index < settingCount; ++index) if (settings[index].uid == uid) { found = index; break; }
  if (found == settingCount) return false;
  Preferences preferences;
  const String ns = settingNamespace(uid);
  if (preferences.begin(ns.c_str(), false)) { preferences.clear(); preferences.end(); }
  for (size_t index = found + 1; index < settingCount; ++index) settings[index - 1] = settings[index];
  --settingCount;
  return saveKnownList();
}
