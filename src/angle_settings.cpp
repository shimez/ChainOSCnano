#include "angle_settings.h"

#include <Preferences.h>
#include <ctype.h>
#include <math.h>

#include "logging.h"
#include "compact_storage.h"
#include "device_file_storage.h"

namespace {

constexpr size_t MAX_ANGLE_SETTINGS = 40;
constexpr char STORAGE_VERSION[] = "A1";
AngleSetting settings[MAX_ANGLE_SETTINGS];
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
  snprintf(name, sizeof(name), "a%08X",
           static_cast<unsigned>(identityHash(identity)));
  return String(name);
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

bool sameSetting(const AngleSetting& left, const AngleSetting& right) {
  return left.identity == right.identity &&
         left.displayName == right.displayName && left.address == right.address &&
         left.use12Bit == right.use12Bit && left.deadband == right.deadband &&
         fabsf(left.outputMin - right.outputMin) <= 0.00001f &&
         fabsf(left.outputMax - right.outputMax) <= 0.00001f &&
         left.outputType == right.outputType;
}

bool loadSetting(const String& identity, AngleSetting& setting, bool& found) {
  found = false;
  const DeviceFileLoadResult result = deviceFileStorageLoad(setting);
  if (result == DeviceFileLoadResult::Loaded) { found = true; return true; }
  if (result == DeviceFileLoadResult::Error) { found = true; return false; }
  if (compactStorageLoad(compactStorageNamespace(identity), setting)) {
    found = true;
    if (deviceFileStorageSave(setting))
      NANO_STORAGE_LOGF("[ChainOSCnano][ANGLECFG] migrated identity=%s source=NVS target=LittleFS\n", identity.c_str());
    return true;
  }
  return false;

}

bool writeSetting(const AngleSetting& setting) {
  if (!deviceFileStorageSave(setting)) return false;
  AngleSetting verify = setting;
  bool found = false;
  return loadSetting(setting.identity, verify, found) && found &&
         sameSetting(setting, verify);
}

void saveKnownDevices() {
  // LittleFS files are the catalog; NVS is migration-only.
}

}  // namespace

void angleSettingsSetup() {
  deviceFileStorageBegin();
  String fileIdentities[MAX_ANGLE_SETTINGS];
  const size_t fileCount = deviceFileStorageList("angle", fileIdentities, MAX_ANGLE_SETTINGS);
  loadingKnown = true;
  for (size_t i = 0; i < fileCount; ++i)
    if (fileIdentities[i].startsWith("chain:") && fileIdentities[i].length() > 6)
      angleSettingsEnsure(fileIdentities[i], String("Chain Angle ") + fileIdentities[i].substring(6));
  loadingKnown = false;
  Preferences preferences;
  String known;
  if (preferences.begin("anglecfg", true)) {
    known = preferences.getString("known", "");
    preferences.end();
  }
  loadingKnown = true;
  int offset = 0;
  while (offset < static_cast<int>(known.length())) {
    int end = known.indexOf('\n', offset);
    if (end < 0) end = known.length();
    const String identity = known.substring(offset, end);
    if (identity.startsWith("chain:") && identity.length() > 6)
      angleSettingsEnsure(identity, String("Chain Angle ") + identity.substring(6));
    offset = end + 1;
  }
  loadingKnown = false;
  NANO_VERBOSE_LOGF("[ChainOSCnano][ANGLECFG] setup_complete settings=%u\n",
                static_cast<unsigned>(settingCount));
}

AngleSetting* angleSettingsEnsure(const String& identity,
                                  const String& defaultName) {
  for (size_t i = 0; i < settingCount; ++i)
    if (settings[i].identity == identity) return &settings[i];
  if (settingCount >= MAX_ANGLE_SETTINGS) return nullptr;
  AngleSetting& setting = settings[settingCount++];
  setting.identity = identity;
  setting.displayName = defaultName;
  bool found = false;
  loadSetting(identity, setting, found);
  saveKnownDevices();
  return &setting;
}

size_t angleSettingsCount() { return settingCount; }

AngleSetting* angleSettingsAt(size_t index) {
  return index < settingCount ? &settings[index] : nullptr;
}

bool angleSettingsSave(const AngleSetting& candidate) {
  if (candidate.identity.isEmpty() || candidate.displayName.isEmpty() ||
      candidate.displayName.length() > 64 || !validAddress(candidate.address) ||
      candidate.deadband < 1 || !isfinite(candidate.outputMin) ||
      !isfinite(candidate.outputMax))
    return false;
  AngleSetting* destination = nullptr;
  for (size_t i = 0; i < settingCount; ++i)
    if (settings[i].identity == candidate.identity) destination = &settings[i];
  if (!destination || !writeSetting(candidate)) return false;
  const uint8_t portMask = destination->connectedPortMask;
  *destination = candidate;
  destination->connectedPortMask = portMask;
  NANO_VERBOSE_LOGF("[ChainOSCnano][ANGLECFG] saved identity=%s resolution=%u deadband=%d\n",
                candidate.identity.c_str(), candidate.use12Bit ? 12 : 8,
                candidate.deadband);
  return true;
}

bool angleSettingsDelete(const String& identity) {
  size_t found = settingCount;
  for (size_t i = 0; i < settingCount; ++i) {
    if (settings[i].identity == identity) {
      if (settings[i].connectedPortMask != 0) return false;
      found = i;
      break;
    }
  }
  if (found == settingCount) return false;
  if (!deviceFileStorageRemove("angle", identity)) return false;
  compactStorageDelete(identity);
  for (size_t i = found + 1; i < settingCount; ++i) settings[i - 1] = settings[i];
  --settingCount;
  settings[settingCount] = AngleSetting();
  saveKnownDevices();
  return true;
}

void angleSettingsBeginPortUpdate(uint8_t portMask) {
  for (size_t i = 0; i < settingCount; ++i)
    settings[i].connectedPortMask &= ~portMask;
}

void angleSettingsMarkConnected(const String& identity, uint8_t portMask) {
  for (size_t i = 0; i < settingCount; ++i)
    if (settings[i].identity == identity) {
      settings[i].connectedPortMask |= portMask;
      return;
    }
}
