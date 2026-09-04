#include "device_file_storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>

#include "config.h"

namespace {

constexpr char ROOT_DIR[] = "/device-settings";
constexpr char FILE_FORMAT[] = "ChainOSCnano-device-setting";
constexpr uint8_t FILE_VERSION = 1;
bool mounted = false;

bool ensureDirectory(const String& path) {
  if (LittleFS.exists(path)) return true;
  return LittleFS.mkdir(path);
}

String typeDirectory(const char* type) {
  return String(ROOT_DIR) + "/" + type;
}

String fileStem(const String& identity) {
  if (identity == "nano:button") return "NanoButton";
  if (identity.startsWith("chain:") && identity.length() > 6)
    return identity.substring(6);
  String safe = identity;
  safe.replace("/", "_");
  safe.replace("\\", "_");
  safe.replace(":", "_");
  return safe;
}

String settingPath(const char* type, const String& identity) {
  return typeDirectory(type) + "/" + fileStem(identity) + ".json";
}

bool prepareType(const char* type) {
  return deviceFileStorageBegin() && ensureDirectory(ROOT_DIR) &&
         ensureDirectory(typeDirectory(type));
}

void logResult(const char* operation, const char* type, const String& identity,
               const String& path, size_t fileBytes, const char* result,
               const char* reason = "none") {
  const size_t total = mounted ? LittleFS.totalBytes() : 0;
  const size_t used = mounted ? LittleFS.usedBytes() : 0;
  Serial.printf(
      "[ChainOSCnano][LITTLEFS] operation=%s type=%s identity=%s path=%s "
      "file_bytes=%u total_bytes=%u used_bytes=%u free_bytes=%u result=%s "
      "reason=%s\n",
      operation, type, identity.c_str(), path.c_str(),
      static_cast<unsigned>(fileBytes), static_cast<unsigned>(total),
      static_cast<unsigned>(used),
      static_cast<unsigned>(total >= used ? total - used : 0), result, reason);
}

void addMessage(JsonObject object, const KeyOscMessage& message) {
  object["address"] = message.address;
  object["value"] = message.valueStr;
  object["type"] = static_cast<uint8_t>(message.valueType);
}

void addMessages(JsonObject root, const char* name,
                 const KeyOscMessage* messages, uint8_t count) {
  JsonArray array = root[name].to<JsonArray>();
  for (uint8_t i = 0; i < count; ++i)
    addMessage(array.add<JsonObject>(), messages[i]);
}

void addSequence(JsonObject object, const KeySequenceConfig& sequence) {
  object["address"] = sequence.address;
  object["type"] = static_cast<uint8_t>(sequence.valueType);
  object["start"] = sequence.start;
  object["end"] = sequence.end;
  object["step"] = sequence.step;
}

bool readMessage(JsonObjectConst object, KeyOscMessage& message) {
  if (!object["address"].is<const char*>() ||
      !object["value"].is<const char*>() || !object["type"].is<int>())
    return false;
  const int type = object["type"].as<int>();
  if (type < TYPE_FLOAT || type > TYPE_STRING) return false;
  message.address = object["address"].as<const char*>();
  message.valueStr = object["value"].as<const char*>();
  message.valueType = static_cast<ValueType>(type);
  return true;
}

bool readMessages(JsonObjectConst root, const char* name,
                  KeyOscMessage* messages, uint8_t& count) {
  JsonArrayConst array = root[name].as<JsonArrayConst>();
  if (array.isNull() || array.size() > MAX_KEY_OSC_MESSAGES) return false;
  count = static_cast<uint8_t>(array.size());
  uint8_t index = 0;
  for (JsonObjectConst object : array)
    if (!readMessage(object, messages[index++])) return false;
  return true;
}

bool readSequence(JsonObjectConst object, KeySequenceConfig& sequence) {
  if (!object["address"].is<const char*>() || !object["type"].is<int>() ||
      !object.containsKey("start") || !object.containsKey("end") ||
      !object.containsKey("step"))
    return false;
  const int type = object["type"].as<int>();
  if (type < TYPE_FLOAT || type > TYPE_STRING) return false;
  sequence.address = object["address"].as<const char*>();
  sequence.valueType = static_cast<ValueType>(type);
  sequence.start = object["start"].as<float>();
  sequence.end = object["end"].as<float>();
  sequence.step = object["step"].as<float>();
  sequence.current = sequence.start;
  return isfinite(sequence.start) && isfinite(sequence.end) &&
         isfinite(sequence.step);
}

template <typename Fill>
bool saveDocument(const char* type, const String& identity,
                  const String& displayName, Fill fill) {
  if (!prepareType(type)) {
    logResult("save", type, identity, "", 0, "failed", "mount_failed");
    return false;
  }

  JsonDocument document;
  JsonObject root = document.to<JsonObject>();
  root["format"] = FILE_FORMAT;
  root["version"] = FILE_VERSION;
  root["type"] = type;
  root["identity"] = identity;
  root["displayName"] = displayName;
  fill(root);

  const String path = settingPath(type, identity);
  const String temporary = path + ".tmp";
  const String backup = path + ".bak";
  LittleFS.remove(temporary);

  File file = LittleFS.open(temporary, FILE_WRITE);
  if (!file) {
    logResult("save", type, identity, path, 0, "failed", "open_temp_failed");
    return false;
  }

  const size_t written = serializeJson(document, file);
  file.flush();
  file.close();

  File sizeFile = LittleFS.open(temporary, FILE_READ);
  const size_t fileBytes = sizeFile ? sizeFile.size() : 0;
  if (sizeFile) sizeFile.close();

  if (written == 0 || fileBytes != written) {
    LittleFS.remove(temporary);
    logResult("save", type, identity, path, fileBytes, "failed",
              "short_write");
    return false;
  }

  File verifyFile = LittleFS.open(temporary, FILE_READ);
  if (!verifyFile) {
    LittleFS.remove(temporary);
    logResult("save", type, identity, path, fileBytes, "failed",
              "temp_open_failed");
    return false;
  }

  JsonDocument verify;
  const DeserializationError parseError = deserializeJson(verify, verifyFile);
  verifyFile.close();

  if (parseError) {
    LittleFS.remove(temporary);
    logResult("save", type, identity, path, fileBytes, "failed",
              "temp_parse_failed");
    return false;
  }

  if (String(verify["format"].as<const char*>()) != FILE_FORMAT ||
      verify["version"].as<int>() != FILE_VERSION ||
      String(verify["type"].as<const char*>()) != type ||
      String(verify["identity"].as<const char*>()) != identity ||
      String(verify["displayName"].as<const char*>()) != displayName) {
    LittleFS.remove(temporary);
    logResult("save", type, identity, path, fileBytes, "failed",
              "temp_header_invalid");
    return false;
  }

  LittleFS.remove(backup);
  const bool hadCurrent = LittleFS.exists(path);
  if (hadCurrent && !LittleFS.rename(path, backup)) {
    LittleFS.remove(temporary);
    logResult("save", type, identity, path, fileBytes, "failed",
              "backup_failed");
    return false;
  }

  if (!LittleFS.rename(temporary, path)) {
    if (hadCurrent) LittleFS.rename(backup, path);
    LittleFS.remove(temporary);
    logResult("save", type, identity, path, fileBytes, "failed",
              "replace_failed");
    return false;
  }

  LittleFS.remove(backup);
  logResult("save", type, identity, path, fileBytes, "ok");
  return true;
}

DeviceFileLoadResult loadDocument(const char* type, const String& identity,
                                  JsonDocument& document, size_t& fileBytes) {
  fileBytes = 0;
  if (!prepareType(type)) return DeviceFileLoadResult::Error;
  const String path = settingPath(type, identity);
  if (!LittleFS.exists(path)) return DeviceFileLoadResult::NotFound;
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    logResult("load", type, identity, path, 0, "failed", "open_failed");
    return DeviceFileLoadResult::Error;
  }
  fileBytes = file.size();
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error || String(document["format"].as<const char*>()) != FILE_FORMAT ||
      document["version"].as<int>() != FILE_VERSION ||
      String(document["type"].as<const char*>()) != type ||
      String(document["identity"].as<const char*>()) != identity ||
      !document["displayName"].is<const char*>()) {
    logResult("load", type, identity, path, fileBytes, "failed",
              error ? error.c_str() : "header_invalid");
    return DeviceFileLoadResult::Error;
  }
  logResult("load", type, identity, path, fileBytes, "ok");
  return DeviceFileLoadResult::Loaded;
}

bool readCommon(JsonDocument& document, String& displayName) {
  displayName = document["displayName"].as<const char*>();
  return !displayName.isEmpty() && displayName.length() <= 64;
}

}  // namespace

bool deviceFileStorageBegin() {
  if (mounted) return true;
  mounted = LittleFS.begin(true);
  if (!mounted) {
    Serial.println("[ChainOSCnano][LITTLEFS] mount result=failed");
    return false;
  }
  ensureDirectory(ROOT_DIR);
  deviceFileStorageLogUsage("mount");
  return true;
}

void deviceFileStorageLogUsage(const char* phase) {
  if (!mounted) return;
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  Serial.printf("[ChainOSCnano][LITTLEFS] phase=%s total_bytes=%u used_bytes=%u free_bytes=%u\n",
                phase, static_cast<unsigned>(total),
                static_cast<unsigned>(used),
                static_cast<unsigned>(total >= used ? total - used : 0));
}

bool deviceFileStorageSave(const KeySetting& setting) {
  return saveDocument("key", setting.identity, setting.displayName,
                      [&](JsonObject root) {
    root["mode"] = static_cast<uint8_t>(setting.mode);
    addMessages(root, "press", setting.pressMessages,
                setting.pressMessageCount);
    addMessages(root, "release", setting.releaseMessages,
                setting.releaseMessageCount);
    addSequence(root["sequence"].to<JsonObject>(), setting.sequence);
  });
}

bool deviceFileStorageSave(const EncoderSetting& setting) {
  return saveDocument("encoder", setting.identity, setting.displayName,
                      [&](JsonObject root) {
    root["rotationAddress"] = setting.rotationAddress;
    root["sendIncrement"] = setting.sendIncrement;
    root["wrapAround"] = setting.wrapAround;
    root["absoluteInputMin"] = setting.absoluteInputMin;
    root["absoluteInputMax"] = setting.absoluteInputMax;
    root["incrementScale"] = setting.incrementScale;
    root["outputMin"] = setting.outputMin;
    root["outputMax"] = setting.outputMax;
    root["outputType"] = static_cast<uint8_t>(setting.outputType);
    root["clickMode"] = static_cast<uint8_t>(setting.clickMode);
    addMessages(root, "press", setting.pressMessages,
                setting.pressMessageCount);
    addMessages(root, "release", setting.releaseMessages,
                setting.releaseMessageCount);
    addSequence(root["sequence"].to<JsonObject>(), setting.clickSequence);
  });
}

bool deviceFileStorageSave(const AngleSetting& setting) {
  return saveDocument("angle", setting.identity, setting.displayName,
                      [&](JsonObject root) {
    root["address"] = setting.address;
    root["use12Bit"] = setting.use12Bit;
    root["deadband"] = setting.deadband;
    root["outputMin"] = setting.outputMin;
    root["outputMax"] = setting.outputMax;
    root["outputType"] = static_cast<uint8_t>(setting.outputType);
  });
}

bool deviceFileStorageSave(const TofSetting& setting) {
  return saveDocument("tof", setting.identity, setting.displayName,
                      [&](JsonObject root) {
    root["address"] = setting.address;
    root["deadband"] = setting.deadband;
    root["maxDistanceMm"] = setting.maxDistanceMm;
    root["nearValueHigh"] = setting.nearValueHigh;
    root["outputMin"] = setting.outputMin;
    root["outputMax"] = setting.outputMax;
    root["outputType"] = static_cast<uint8_t>(setting.outputType);
  });
}

bool deviceFileStorageSave(const JoystickSetting& setting) {
  return saveDocument("joystick", setting.identity, setting.displayName,
                      [&](JsonObject root) {
    root["xAddress"] = setting.xAddress;
    root["yAddress"] = setting.yAddress;
    root["deadband"] = setting.deadband;
    root["invertX"] = setting.invertX;
    root["invertY"] = setting.invertY;
    root["outputMin"] = setting.outputMin;
    root["outputMax"] = setting.outputMax;
    root["outputType"] = static_cast<uint8_t>(setting.outputType);
    root["clickMode"] = static_cast<uint8_t>(setting.clickMode);
    addMessages(root, "press", setting.pressMessages,
                setting.pressMessageCount);
    addMessages(root, "release", setting.releaseMessages,
                setting.releaseMessageCount);
    addSequence(root["sequence"].to<JsonObject>(), setting.clickSequence);
  });
}

DeviceFileLoadResult deviceFileStorageLoad(KeySetting& setting) {
  JsonDocument document; size_t bytes;
  const auto result = loadDocument("key", setting.identity, document, bytes);
  if (result != DeviceFileLoadResult::Loaded) return result;
  KeySetting candidate = setting;
  const int mode = document["mode"] | -1;
  if (!readCommon(document, candidate.displayName) || mode < MODE_PRESS_RELEASE ||
      mode > MODE_SEQUENCE ||
      !readMessages(document.as<JsonObjectConst>(), "press",
                    candidate.pressMessages, candidate.pressMessageCount) ||
      !readMessages(document.as<JsonObjectConst>(), "release",
                    candidate.releaseMessages, candidate.releaseMessageCount) ||
      candidate.pressMessageCount + candidate.releaseMessageCount >
          MAX_KEY_OSC_MESSAGES ||
      !readSequence(document["sequence"].as<JsonObjectConst>(),
                    candidate.sequence))
    return DeviceFileLoadResult::Error;
  candidate.mode = static_cast<KeyMode>(mode);
  setting = candidate;
  return DeviceFileLoadResult::Loaded;
}

DeviceFileLoadResult deviceFileStorageLoad(EncoderSetting& setting) {
  JsonDocument document; size_t bytes;
  const auto result = loadDocument("encoder", setting.identity, document, bytes);
  if (result != DeviceFileLoadResult::Loaded) return result;
  EncoderSetting c = setting;
  const int outputType = document["outputType"] | -1;
  const int clickMode = document["clickMode"] | -1;
  if (!readCommon(document, c.displayName) ||
      !document["rotationAddress"].is<const char*>() ||
      outputType < TYPE_FLOAT || outputType > TYPE_STRING ||
      clickMode < MODE_PRESS_RELEASE || clickMode > MODE_SEQUENCE ||
      !readMessages(document.as<JsonObjectConst>(), "press", c.pressMessages,
                    c.pressMessageCount) ||
      !readMessages(document.as<JsonObjectConst>(), "release", c.releaseMessages,
                    c.releaseMessageCount) ||
      c.pressMessageCount + c.releaseMessageCount > MAX_KEY_OSC_MESSAGES ||
      !readSequence(document["sequence"].as<JsonObjectConst>(), c.clickSequence))
    return DeviceFileLoadResult::Error;
  c.rotationAddress = document["rotationAddress"].as<const char*>();
  c.sendIncrement = document["sendIncrement"] | false;
  c.wrapAround = document["wrapAround"] | true;
  c.absoluteInputMin = document["absoluteInputMin"].as<float>();
  c.absoluteInputMax = document["absoluteInputMax"].as<float>();
  c.incrementScale = document["incrementScale"].as<float>();
  c.outputMin = document["outputMin"].as<float>();
  c.outputMax = document["outputMax"].as<float>();
  c.outputType = static_cast<ValueType>(outputType);
  c.clickMode = static_cast<KeyMode>(clickMode);
  setting = c;
  return DeviceFileLoadResult::Loaded;
}

DeviceFileLoadResult deviceFileStorageLoad(AngleSetting& setting) {
  JsonDocument document; size_t bytes;
  const auto result = loadDocument("angle", setting.identity, document, bytes);
  if (result != DeviceFileLoadResult::Loaded) return result;
  AngleSetting c = setting;
  const int outputType = document["outputType"] | -1;
  if (!readCommon(document, c.displayName) ||
      !document["address"].is<const char*>() || outputType < TYPE_FLOAT ||
      outputType > TYPE_STRING)
    return DeviceFileLoadResult::Error;
  c.address = document["address"].as<const char*>();
  c.use12Bit = document["use12Bit"] | true;
  c.deadband = document["deadband"].as<int>();
  c.outputMin = document["outputMin"].as<float>();
  c.outputMax = document["outputMax"].as<float>();
  c.outputType = static_cast<ValueType>(outputType);
  setting = c;
  return DeviceFileLoadResult::Loaded;
}

DeviceFileLoadResult deviceFileStorageLoad(TofSetting& setting) {
  JsonDocument document; size_t bytes;
  const auto result = loadDocument("tof", setting.identity, document, bytes);
  if (result != DeviceFileLoadResult::Loaded) return result;
  TofSetting c = setting;
  const int outputType = document["outputType"] | -1;
  if (!readCommon(document, c.displayName) ||
      !document["address"].is<const char*>() || outputType < TYPE_FLOAT ||
      outputType > TYPE_INT)
    return DeviceFileLoadResult::Error;
  c.address = document["address"].as<const char*>();
  c.deadband = document["deadband"].as<int>();
  c.maxDistanceMm = document["maxDistanceMm"].as<int>();
  c.nearValueHigh = document["nearValueHigh"] | false;
  c.outputMin = document["outputMin"].as<float>();
  c.outputMax = document["outputMax"].as<float>();
  c.outputType = static_cast<ValueType>(outputType);
  setting = c;
  return DeviceFileLoadResult::Loaded;
}

DeviceFileLoadResult deviceFileStorageLoad(JoystickSetting& setting) {
  JsonDocument document; size_t bytes;
  const auto result = loadDocument("joystick", setting.identity, document, bytes);
  if (result != DeviceFileLoadResult::Loaded) return result;
  JoystickSetting c = setting;
  const int outputType = document["outputType"] | -1;
  const int clickMode = document["clickMode"] | -1;
  if (!readCommon(document, c.displayName) ||
      !document["xAddress"].is<const char*>() ||
      !document["yAddress"].is<const char*>() || outputType < TYPE_FLOAT ||
      outputType > TYPE_STRING || clickMode < MODE_PRESS_RELEASE ||
      clickMode > MODE_SEQUENCE ||
      !readMessages(document.as<JsonObjectConst>(), "press", c.pressMessages,
                    c.pressMessageCount) ||
      !readMessages(document.as<JsonObjectConst>(), "release", c.releaseMessages,
                    c.releaseMessageCount) ||
      c.pressMessageCount + c.releaseMessageCount > MAX_KEY_OSC_MESSAGES ||
      !readSequence(document["sequence"].as<JsonObjectConst>(), c.clickSequence))
    return DeviceFileLoadResult::Error;
  c.xAddress = document["xAddress"].as<const char*>();
  c.yAddress = document["yAddress"].as<const char*>();
  c.deadband = document["deadband"].as<int>();
  c.invertX = document["invertX"] | false;
  c.invertY = document["invertY"] | false;
  c.outputMin = document["outputMin"].as<float>();
  c.outputMax = document["outputMax"].as<float>();
  c.outputType = static_cast<ValueType>(outputType);
  c.clickMode = static_cast<KeyMode>(clickMode);
  setting = c;
  return DeviceFileLoadResult::Loaded;
}

bool deviceFileStorageRemove(const char* type, const String& identity) {
  if (!prepareType(type)) return false;
  const String path = settingPath(type, identity);
  const bool removed = !LittleFS.exists(path) || LittleFS.remove(path);
  logResult("remove", type, identity, path, 0, removed ? "ok" : "failed",
            removed ? "none" : "remove_failed");
  return removed;
}

size_t deviceFileStorageList(const char* type, String* identities,
                             size_t capacity) {
  if (!prepareType(type)) return 0;
  File directory = LittleFS.open(typeDirectory(type));
  if (!directory || !directory.isDirectory()) return 0;
  size_t count = 0;
  File file = directory.openNextFile();
  while (file) {
    const String name = file.name();
    if (!file.isDirectory() && name.endsWith(".json") && count < capacity) {
      JsonDocument document;
      if (!deserializeJson(document, file) &&
          String(document["format"].as<const char*>()) == FILE_FORMAT &&
          document["version"].as<int>() == FILE_VERSION &&
          String(document["type"].as<const char*>()) == type &&
          document["identity"].is<const char*>()) {
        identities[count++] = document["identity"].as<const char*>();
      }
    }
    file.close();
    file = directory.openNextFile();
  }
  directory.close();
  Serial.printf("[ChainOSCnano][LITTLEFS] operation=list type=%s files=%u\n",
                type, static_cast<unsigned>(count));
  return count;
}
