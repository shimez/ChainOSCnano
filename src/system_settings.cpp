#include "system_settings.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "config.h"
#include "device_file_storage.h"

namespace {

constexpr char SETTINGS_DIR[] = "/system";
constexpr char SETTINGS_PATH[] = "/system/settings.json";
constexpr char SETTINGS_TEMP_PATH[] = "/system/settings.json.tmp";
constexpr char SETTINGS_BACKUP_PATH[] = "/system/settings.json.bak";
constexpr char FILE_FORMAT[] = "ChainOSCnano-system-settings";
constexpr uint8_t FILE_VERSION = 1;
constexpr char DEFAULT_OSC_HOST[] = "192.168.1.100";
constexpr uint16_t DEFAULT_OSC_PORT = 9000;

struct Settings {
  String ssid;
  String password;
  String oscHost = DEFAULT_OSC_HOST;
  uint16_t oscPort = DEFAULT_OSC_PORT;
  bool languageConfigured = false;
  uint8_t language = 0;
};

Settings current;
bool initialized = false;

void logResult(const char* operation, size_t fileBytes, const char* result,
               const char* reason = "none") {
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  Serial.printf(
      "[ChainOSCnano][SYSTEM] operation=%s path=%s file_bytes=%u "
      "total_bytes=%u used_bytes=%u free_bytes=%u result=%s reason=%s\n",
      operation, SETTINGS_PATH, static_cast<unsigned>(fileBytes),
      static_cast<unsigned>(total), static_cast<unsigned>(used),
      static_cast<unsigned>(total >= used ? total - used : 0), result, reason);
}

bool isValid(const Settings& settings) {
  return settings.ssid.length() <= 32 && settings.password.length() <= 64 &&
         !settings.oscHost.isEmpty() && settings.oscHost.length() <= 253 &&
         settings.oscPort != 0 &&
         (!settings.languageConfigured || settings.language <= 1);
}

bool decode(JsonDocument& document, Settings& settings) {
  if (!document["format"].is<const char*>() ||
      String(document["format"].as<const char*>()) != FILE_FORMAT ||
      !document["version"].is<int>() ||
      document["version"].as<int>() != FILE_VERSION ||
      !document["wifi"].is<JsonObjectConst>() ||
      !document["osc"].is<JsonObjectConst>() ||
      !document["ui"].is<JsonObjectConst>())
    return false;

  JsonObjectConst wifi = document["wifi"].as<JsonObjectConst>();
  JsonObjectConst osc = document["osc"].as<JsonObjectConst>();
  JsonObjectConst ui = document["ui"].as<JsonObjectConst>();
  if (!wifi["ssid"].is<const char*>() ||
      !wifi["password"].is<const char*>() ||
      !osc["host"].is<const char*>() || !osc["port"].is<int>() ||
      !ui["configured"].is<bool>() || !ui["language"].is<int>())
    return false;

  Settings candidate;
  candidate.ssid = wifi["ssid"].as<const char*>();
  candidate.password = wifi["password"].as<const char*>();
  candidate.oscHost = osc["host"].as<const char*>();
  const int port = osc["port"].as<int>();
  if (port < 1 || port > 65535) return false;
  candidate.oscPort = static_cast<uint16_t>(port);
  candidate.languageConfigured = ui["configured"].as<bool>();
  candidate.language = static_cast<uint8_t>(ui["language"].as<int>());
  if (!isValid(candidate)) return false;
  settings = candidate;
  return true;
}

bool loadFile(Settings& settings) {
  File file = LittleFS.open(SETTINGS_PATH, FILE_READ);
  if (!file) {
    logResult("load", 0, "failed", "open_failed");
    return false;
  }
  const size_t bytes = file.size();
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) {
    logResult("load", bytes, "failed", error.c_str());
    return false;
  }
  if (!decode(document, settings)) {
    logResult("load", bytes, "failed", "content_invalid");
    return false;
  }
  logResult("load", bytes, "ok");
  return true;
}

bool saveFile(const Settings& settings, const char* operation = "save") {
  if (!isValid(settings)) {
    logResult(operation, 0, "failed", "content_invalid");
    return false;
  }
  if (!LittleFS.exists(SETTINGS_DIR) && !LittleFS.mkdir(SETTINGS_DIR)) {
    logResult(operation, 0, "failed", "mkdir_failed");
    return false;
  }

  JsonDocument document;
  JsonObject root = document.to<JsonObject>();
  root["format"] = FILE_FORMAT;
  root["version"] = FILE_VERSION;
  JsonObject wifi = root["wifi"].to<JsonObject>();
  wifi["ssid"] = settings.ssid;
  wifi["password"] = settings.password;
  JsonObject osc = root["osc"].to<JsonObject>();
  osc["host"] = settings.oscHost;
  osc["port"] = settings.oscPort;
  JsonObject ui = root["ui"].to<JsonObject>();
  ui["configured"] = settings.languageConfigured;
  ui["language"] = settings.language;

  LittleFS.remove(SETTINGS_TEMP_PATH);
  File file = LittleFS.open(SETTINGS_TEMP_PATH, FILE_WRITE);
  if (!file) {
    logResult(operation, 0, "failed", "open_temp_failed");
    return false;
  }
  const size_t written = serializeJson(document, file);
  file.flush();
  file.close();

  File verifyFile = LittleFS.open(SETTINGS_TEMP_PATH, FILE_READ);
  const size_t bytes = verifyFile ? verifyFile.size() : 0;
  JsonDocument verifyDocument;
  const DeserializationError verifyError = verifyFile
      ? deserializeJson(verifyDocument, verifyFile)
      : DeserializationError::EmptyInput;
  if (verifyFile) verifyFile.close();
  Settings verified;
  if (written == 0 || bytes != written || verifyError ||
      !decode(verifyDocument, verified)) {
    LittleFS.remove(SETTINGS_TEMP_PATH);
    logResult(operation, bytes, "failed",
              verifyError ? verifyError.c_str() : "temp_verify_failed");
    return false;
  }

  LittleFS.remove(SETTINGS_BACKUP_PATH);
  const bool hadCurrent = LittleFS.exists(SETTINGS_PATH);
  if (hadCurrent && !LittleFS.rename(SETTINGS_PATH, SETTINGS_BACKUP_PATH)) {
    LittleFS.remove(SETTINGS_TEMP_PATH);
    logResult(operation, bytes, "failed", "backup_failed");
    return false;
  }
  if (!LittleFS.rename(SETTINGS_TEMP_PATH, SETTINGS_PATH)) {
    if (hadCurrent) LittleFS.rename(SETTINGS_BACKUP_PATH, SETTINGS_PATH);
    LittleFS.remove(SETTINGS_TEMP_PATH);
    logResult(operation, bytes, "failed", "replace_failed");
    return false;
  }
  LittleFS.remove(SETTINGS_BACKUP_PATH);
  logResult(operation, bytes, "ok");
  return true;
}

Settings readLegacyNvs() {
  Settings settings;
  Preferences preferences;
  if (preferences.begin(WIFI_PREFS_NAMESPACE, true)) {
    settings.ssid = preferences.getString("ssid", "");
    settings.password = preferences.getString("password", "");
    settings.oscHost = preferences.getString("osc_host", DEFAULT_OSC_HOST);
    const uint32_t port = preferences.getUInt("osc_port", DEFAULT_OSC_PORT);
    if (port >= 1 && port <= 65535)
      settings.oscPort = static_cast<uint16_t>(port);
    preferences.end();
  }
  if (preferences.begin("ui", true)) {
    const uint8_t language = preferences.getUChar("language", 0xff);
    if (language <= 1) {
      settings.languageConfigured = true;
      settings.language = language;
    }
    preferences.end();
  }
  return settings;
}

bool commit(const Settings& candidate) {
  if (!saveFile(candidate)) return false;
  current = candidate;
  return true;
}

}  // namespace

bool systemSettingsSetup() {
  if (initialized) return true;
  if (!deviceFileStorageBegin()) {
    Serial.println(
        "[ChainOSCnano][SYSTEM] operation=setup result=failed reason=mount_failed");
    return false;
  }
  if (LittleFS.exists(SETTINGS_PATH) && loadFile(current)) {
    initialized = true;
    return true;
  }

  current = readLegacyNvs();
  if (!saveFile(current, "migrate")) return false;
  initialized = true;
  Serial.println(
      "[ChainOSCnano][SYSTEM] migration source=nvs target=littlefs result=ok");
  return true;
}

const String& systemSettingsWifiSsid() { return current.ssid; }
const String& systemSettingsWifiPassword() { return current.password; }
const String& systemSettingsOscHost() { return current.oscHost; }
uint16_t systemSettingsOscPort() { return current.oscPort; }
bool systemSettingsHasUiLanguage() { return current.languageConfigured; }
uint8_t systemSettingsUiLanguage() { return current.language; }

bool systemSettingsSaveWifi(const String& ssid, const String& password) {
  Settings candidate = current;
  candidate.ssid = ssid;
  candidate.password = password;
  return commit(candidate);
}

bool systemSettingsClearWifi() { return systemSettingsSaveWifi("", ""); }

bool systemSettingsSaveOsc(const String& host, uint16_t port) {
  Settings candidate = current;
  candidate.oscHost = host;
  candidate.oscPort = port;
  return commit(candidate);
}

bool systemSettingsSaveUiLanguage(uint8_t language) {
  Settings candidate = current;
  candidate.languageConfigured = true;
  candidate.language = language;
  return commit(candidate);
}
