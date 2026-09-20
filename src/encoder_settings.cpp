#include "encoder_settings.h"

#include <Preferences.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

#include "logging.h"
#include "compact_storage.h"
#include "device_file_storage.h"

namespace {

constexpr size_t MAX_ENCODER_SETTINGS = 40;
constexpr char STORAGE_VERSION[] = "E1";
EncoderSetting settings[MAX_ENCODER_SETTINGS];
size_t settingCount = 0;
bool loadingKnown = false;

uint32_t identityHash(const String& identity) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < identity.length(); ++i) {
    hash ^= static_cast<uint8_t>(identity[i]);
    hash *= 16777619u;
  }
  return hash;
}

String deviceNamespace(const String& identity) {
  char name[11];
  snprintf(name, sizeof(name), "e%08X",
           static_cast<unsigned>(identityHash(identity)));
  return String(name);
}

String indexedKey(const char* prefix, uint8_t index) {
  return String(prefix) + String(static_cast<unsigned>(index));
}

bool validAddress(const String& address) {
  if (address.isEmpty() || address.length() > 192 || address[0] != '/')
    return false;
  for (size_t i = 0; i < address.length(); ++i) {
    const char c = address[i];
    if (isspace(static_cast<unsigned char>(c)) || c == '#' || c == '*' ||
        c == ',' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}')
      return false;
  }
  return true;
}

bool sameMessage(const KeyOscMessage& left, const KeyOscMessage& right) {
  return left.address == right.address && left.valueStr == right.valueStr &&
         left.valueType == right.valueType;
}

bool sameSetting(const EncoderSetting& left, const EncoderSetting& right) {
  if (left.identity != right.identity || left.displayName != right.displayName ||
      left.settingsModel != right.settingsModel ||
      left.rotationAddress != right.rotationAddress ||
      left.outputMin != right.outputMin || left.outputMax != right.outputMax ||
      left.outputType != right.outputType ||
      left.pressMessageCount != right.pressMessageCount ||
      left.releaseMessageCount != right.releaseMessageCount)
    return false;
  if (left.settingsModel == ENCODER_SETTINGS_LEGACY &&
      (left.sendIncrement != right.sendIncrement ||
       left.wrapAround != right.wrapAround ||
       fabsf(left.absoluteInputMin - right.absoluteInputMin) > 0.00001f ||
       fabsf(left.absoluteInputMax - right.absoluteInputMax) > 0.00001f ||
       fabsf(left.incrementScale - right.incrementScale) > 0.00001f ||
       left.clickMode != right.clickMode ||
       left.clickSequence.address != right.clickSequence.address ||
       left.clickSequence.valueType != right.clickSequence.valueType ||
       fabsf(left.clickSequence.start - right.clickSequence.start) > 0.00001f ||
       fabsf(left.clickSequence.end - right.clickSequence.end) > 0.00001f ||
       fabsf(left.clickSequence.step - right.clickSequence.step) > 0.00001f))
    return false;
  if (left.settingsModel == ENCODER_SETTINGS_V2 &&
      (left.rotationMode != right.rotationMode ||
       left.rangeSteps != right.rangeSteps || left.wrapAround != right.wrapAround ||
       left.clockwiseIncreases != right.clockwiseIncreases ||
       left.clockwiseValue != right.clockwiseValue ||
       left.counterClockwiseValue != right.counterClockwiseValue ||
       left.pushMode != right.pushMode ||
       left.resetValue != right.resetValue ||
       left.resetValueConfigured != right.resetValueConfigured ||
       left.clickSequence.address != right.clickSequence.address ||
       left.clickSequence.valueType != right.clickSequence.valueType ||
       fabsf(left.clickSequence.start - right.clickSequence.start) > 0.00001f ||
       fabsf(left.clickSequence.end - right.clickSequence.end) > 0.00001f ||
       fabsf(left.clickSequence.step - right.clickSequence.step) > 0.00001f))
    return false;
  for (uint8_t i = 0; i < left.pressMessageCount; ++i)
    if (!sameMessage(left.pressMessages[i], right.pressMessages[i])) return false;
  for (uint8_t i = 0; i < left.releaseMessageCount; ++i)
    if (!sameMessage(left.releaseMessages[i], right.releaseMessages[i])) return false;
  return true;
}

bool loadSetting(const String& identity, EncoderSetting& setting, bool& found) {
  found = false;
  const DeviceFileLoadResult result = deviceFileStorageLoad(setting);
  if (result == DeviceFileLoadResult::Loaded) { found = true; return true; }
  if (result == DeviceFileLoadResult::Error) { found = true; return false; }
  if (compactStorageLoad(compactStorageNamespace(identity), setting)) {
    found = true;
    if (deviceFileStorageSave(setting))
      NANO_STORAGE_LOGF("[ChainOSCnano][ENCCFG] migrated identity=%s source=NVS target=LittleFS\n", identity.c_str());
    return true;
  }
  return false;

}

bool writeSetting(const EncoderSetting& setting) {
  return deviceFileStorageSave(setting);
}

void saveKnownDevices() {
  // LittleFS files are the catalog; NVS is migration-only.
}

}  // namespace

void encoderSettingsSetup() {
  deviceFileStorageBegin();
  String fileIdentities[MAX_ENCODER_SETTINGS];
  const size_t fileCount = deviceFileStorageList("encoder", fileIdentities, MAX_ENCODER_SETTINGS);
  loadingKnown = true;
  for (size_t i = 0; i < fileCount; ++i)
    if (fileIdentities[i].startsWith("chain:") && fileIdentities[i].length() > 6)
      encoderSettingsEnsure(fileIdentities[i], String("Chain Encoder ") + fileIdentities[i].substring(6));
  loadingKnown = false;
  Preferences preferences;
  String known;
  if (preferences.begin("enccfg", true)) {
    known = preferences.getString("known", "");
    preferences.end();
  }
  loadingKnown = true;
  int offset = 0;
  while (offset < static_cast<int>(known.length())) {
    int end = known.indexOf('\n', offset);
    if (end < 0) end = known.length();
    const String identity = known.substring(offset, end);
    if (identity.startsWith("chain:") && identity.length() > 6) {
      const String uid = identity.substring(6);
      encoderSettingsEnsure(identity, String("Chain Encoder ") + uid);
    }
    offset = end + 1;
  }
  loadingKnown = false;
  NANO_VERBOSE_LOGF("[ChainOSCnano][ENCCFG] setup_complete settings=%u\n",
                static_cast<unsigned>(settingCount));
}

EncoderSetting* encoderSettingsEnsure(const String& identity,
                                      const String& defaultName) {
  for (size_t i = 0; i < settingCount; ++i)
    if (settings[i].identity == identity) return &settings[i];
  if (settingCount >= MAX_ENCODER_SETTINGS) return nullptr;
  EncoderSetting& setting = settings[settingCount++];
  setting.identity = identity;
  setting.displayName = defaultName;
  setting.pressMessages[0].address = "/avatar/parameters/EncoderClick";
  setting.pressMessages[0].valueStr = "1.0";
  setting.pressMessages[0].valueType = TYPE_FLOAT;
  setting.releaseMessages[0].address = "/avatar/parameters/EncoderClick";
  setting.releaseMessages[0].valueStr = "0.0";
  setting.releaseMessages[0].valueType = TYPE_FLOAT;
  setting.clickSequence.address = "/avatar/parameters/EncoderSeq";
  keySettingsNormalizeSequence(setting.clickSequence);
  bool found = false;
  loadSetting(identity, setting, found);
  if (!found) setting.settingsModel = ENCODER_SETTINGS_V2;
  saveKnownDevices();
  return &setting;
}

size_t encoderSettingsCount() { return settingCount; }

EncoderSetting* encoderSettingsAt(size_t index) {
  return index < settingCount ? &settings[index] : nullptr;
}

bool amountOutputIsValid(const EncoderSetting& candidate);
bool directionOutputIsValid(const EncoderSetting& candidate);
bool encoderSettingsRotationResetValueIsValid(const EncoderSetting& candidate);

bool encoderSettingsSave(const EncoderSetting& candidate) {
  if (candidate.identity.isEmpty() || candidate.displayName.isEmpty() ||
      candidate.displayName.length() > 64 ||
      !validAddress(candidate.rotationAddress) ||
      !validAddress(candidate.clickSequence.address) ||
      candidate.pressMessageCount + candidate.releaseMessageCount >
          MAX_KEY_OSC_MESSAGES || !isfinite(candidate.absoluteInputMin) ||
      !isfinite(candidate.absoluteInputMax) ||
      !isfinite(candidate.incrementScale) || !isfinite(candidate.outputMin) ||
      !isfinite(candidate.outputMax) ||
      (candidate.settingsModel == ENCODER_SETTINGS_V2 &&
       (candidate.rotationMode < ENCODER_ROTATION_AMOUNT ||
        candidate.rotationMode > ENCODER_ROTATION_DIRECTION ||
        candidate.rangeSteps < 1 || candidate.pushMode < MODE_PRESS_RELEASE ||
        candidate.pushMode > MODE_ROTATION_RESET ||
        (candidate.rotationMode == ENCODER_ROTATION_AMOUNT
             ? !amountOutputIsValid(candidate)
             : !directionOutputIsValid(candidate)) ||
        !encoderSettingsRotationResetValueIsValid(candidate))))
    return false;
  for (uint8_t i = 0; i < candidate.pressMessageCount; ++i)
    if (!validAddress(candidate.pressMessages[i].address)) return false;
  for (uint8_t i = 0; i < candidate.releaseMessageCount; ++i)
    if (!validAddress(candidate.releaseMessages[i].address)) return false;
  EncoderSetting* destination = nullptr;
  for (size_t i = 0; i < settingCount; ++i)
    if (settings[i].identity == candidate.identity) destination = &settings[i];
  if (!destination || !writeSetting(candidate)) return false;
  const uint8_t portMask = destination->connectedPortMask;
  const bool preserveRuntime =
      destination->settingsModel == ENCODER_SETTINGS_V2 &&
      candidate.settingsModel == ENCODER_SETTINGS_V2 &&
      destination->rotationMode == candidate.rotationMode &&
      destination->rangeSteps == candidate.rangeSteps &&
      destination->wrapAround == candidate.wrapAround &&
      destination->clockwiseIncreases == candidate.clockwiseIncreases &&
      destination->outputMin == candidate.outputMin &&
      destination->outputMax == candidate.outputMax &&
      destination->outputType == candidate.outputType;
  const int32_t logicalPosition = destination->logicalPosition;
  const bool logicalPositionInitialized = destination->logicalPositionInitialized;
  const bool pendingReset = destination->pendingReset;
  const int32_t pendingLowerGrid = destination->pendingLowerGrid;
  const int32_t pendingUpperGrid = destination->pendingUpperGrid;
  *destination = candidate;
  destination->connectedPortMask = portMask;
  destination->boundedAbsoluteInitialized = false;
  if (preserveRuntime) {
    destination->logicalPosition = logicalPosition;
    destination->logicalPositionInitialized = logicalPositionInitialized;
    destination->pendingReset = pendingReset;
    destination->pendingLowerGrid = pendingLowerGrid;
    destination->pendingUpperGrid = pendingUpperGrid;
  } else {
    destination->logicalPositionInitialized = false;
    destination->pendingReset = false;
  }
  keySettingsNormalizeSequence(destination->clickSequence);
  NANO_VERBOSE_LOGF("[ChainOSCnano][ENCCFG] saved identity=%s mode=%u press=%u release=%u\n",
                candidate.identity.c_str(),
                static_cast<unsigned>(candidate.clickMode),
                static_cast<unsigned>(candidate.pressMessageCount),
                static_cast<unsigned>(candidate.releaseMessageCount));
  return true;
}

bool validValueType(ValueType type) {
  return type >= TYPE_FLOAT && type <= TYPE_STRING;
}

bool parseInt32Strict(const String& text, int32_t& value) {
  if (text.isEmpty()) return false;
  size_t index = (text[0] == '+' || text[0] == '-') ? 1 : 0;
  if (index == text.length()) return false;
  for (; index < text.length(); ++index)
    if (!isdigit(static_cast<unsigned char>(text[index]))) return false;
  errno = 0;
  char* end = nullptr;
  const long parsed = strtol(text.c_str(), &end, 10);
  if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
      parsed < INT32_MIN || parsed > INT32_MAX)
    return false;
  value = static_cast<int32_t>(parsed);
  return true;
}

bool parseFloat32Strict(const String& text, float& value) {
  if (text.isEmpty()) return false;
  errno = 0;
  char* end = nullptr;
  const float parsed = strtof(text.c_str(), &end);
  if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
      !isfinite(parsed))
    return false;
  value = parsed;
  return true;
}

bool amountOutputIsValid(const EncoderSetting& candidate) {
  if (!validValueType(candidate.outputType) ||
      !isfinite(candidate.outputMin) || !isfinite(candidate.outputMax) ||
      !(candidate.outputMin < candidate.outputMax) ||
      !isfinite(candidate.outputMax - candidate.outputMin))
    return false;
  if (candidate.outputType != TYPE_INT) return true;
  const double roundedMin = round(static_cast<double>(candidate.outputMin));
  const double roundedMax = round(static_cast<double>(candidate.outputMax));
  return roundedMin >= static_cast<double>(INT32_MIN) &&
         roundedMin <= static_cast<double>(INT32_MAX) &&
         roundedMax >= static_cast<double>(INT32_MIN) &&
         roundedMax <= static_cast<double>(INT32_MAX);
}

bool directionOutputIsValid(const EncoderSetting& candidate) {
  if (!validValueType(candidate.outputType) ||
      candidate.clockwiseValue.length() > 128 ||
      candidate.counterClockwiseValue.length() > 128)
    return false;
  if (candidate.outputType == TYPE_STRING) return true;
  if (candidate.outputType == TYPE_INT) {
    int32_t clockwise = 0;
    int32_t counterClockwise = 0;
    return parseInt32Strict(candidate.clockwiseValue, clockwise) &&
           parseInt32Strict(candidate.counterClockwiseValue, counterClockwise);
  }
  float clockwise = 0;
  float counterClockwise = 0;
  return parseFloat32Strict(candidate.clockwiseValue, clockwise) &&
         parseFloat32Strict(candidate.counterClockwiseValue, counterClockwise);
}

bool encoderSettingsRotationResetValueIsValid(const EncoderSetting& candidate) {
  if (candidate.pushMode != MODE_ROTATION_RESET) return true;
  if (candidate.rotationMode == ENCODER_ROTATION_AMOUNT) {
    float value = 0;
    if (!parseFloat32Strict(candidate.resetValue, value) ||
        value < candidate.outputMin || value > candidate.outputMax)
      return false;
    if (candidate.outputType == TYPE_INT) {
      const double rounded = round(static_cast<double>(value));
      if (rounded < INT32_MIN || rounded > INT32_MAX) return false;
    }
    return true;
  }
  if (candidate.resetValue.length() > 128) return false;
  if (candidate.outputType == TYPE_STRING) return true;
  if (candidate.outputType == TYPE_INT) {
    int32_t value = 0;
    return parseInt32Strict(candidate.resetValue, value);
  }
  float value = 0;
  return parseFloat32Strict(candidate.resetValue, value);
}

namespace {
String legacyDirectionValue(const EncoderSetting& legacy, int direction) {
  const float low = min(legacy.outputMin, legacy.outputMax);
  const float high = max(legacy.outputMin, legacy.outputMax);
  const float value = constrain(direction * legacy.incrementScale, low, high);
  if (legacy.outputType == TYPE_INT) return String((int32_t)lroundf(value));
  return String(value, legacy.outputType == TYPE_STRING ? 3 : 7);
}
}

bool encoderSettingsBuildV2MigrationCandidate(const EncoderSetting& legacy,
                                               EncoderSetting& candidate) {
  if (legacy.settingsModel != ENCODER_SETTINGS_LEGACY) return false;
  candidate = legacy;
  candidate.settingsModel = ENCODER_SETTINGS_V2;
  candidate.pushMode = legacy.clickMode;
  candidate.logicalPosition = 0;
  candidate.logicalPositionInitialized = false;
  if (legacy.sendIncrement) {
    candidate.rotationMode = ENCODER_ROTATION_DIRECTION;
    candidate.clockwiseValue = legacyDirectionValue(legacy, 1);
    candidate.counterClockwiseValue = legacyDirectionValue(legacy, -1);
    return true;
  }
  const float span = legacy.absoluteInputMax - legacy.absoluteInputMin;
  candidate.rotationMode = ENCODER_ROTATION_AMOUNT;
  candidate.rangeSteps = isfinite(span) && floorf(span) == span && span >= 1.0f &&
                                 span <= 65535.0f
                             ? static_cast<uint16_t>(span)
                             : 0;
  candidate.wrapAround = legacy.wrapAround;
  candidate.clockwiseIncreases = true;
  return true;
}

bool encoderSettingsCanLosslesslyMigrate(const EncoderSetting& legacy) {
  if (legacy.settingsModel != ENCODER_SETTINGS_LEGACY || legacy.sendIncrement ||
      legacy.wrapAround || legacy.absoluteInputMin != 0.0f ||
      !(legacy.outputMin < legacy.outputMax))
    return false;
  const float span = legacy.absoluteInputMax - legacy.absoluteInputMin;
  return isfinite(span) && floorf(span) == span && span >= 1.0f && span <= 65535.0f;
}

bool encoderSettingsDelete(const String& identity) {
  size_t found = settingCount;
  for (size_t i = 0; i < settingCount; ++i) {
    if (settings[i].identity == identity) {
      if (settings[i].connectedPortMask != 0) return false;
      found = i;
      break;
    }
  }
  if (found == settingCount) return false;
  if (!deviceFileStorageRemove("encoder", identity)) return false;
  compactStorageDelete(identity);
  for (size_t i = found + 1; i < settingCount; ++i)
    settings[i - 1] = settings[i];
  --settingCount;
  settings[settingCount] = EncoderSetting();
  saveKnownDevices();
  return true;
}

void encoderSettingsResetRuntime(const String& identity) {
  for (size_t i = 0; i < settingCount; ++i) {
    if (settings[i].identity != identity) continue;
    settings[i].logicalPosition = 0;
    settings[i].logicalPositionInitialized = false;
    settings[i].pendingReset = false;
    return;
  }
}

void encoderSettingsBeginPortUpdate(uint8_t portMask) {
  for (size_t i = 0; i < settingCount; ++i)
    settings[i].connectedPortMask &= ~portMask;
}

void encoderSettingsMarkConnected(const String& identity, uint8_t portMask) {
  for (size_t i = 0; i < settingCount; ++i)
    if (settings[i].identity == identity) {
      settings[i].connectedPortMask |= portMask;
      return;
    }
}
