#include "tof_settings.h"

#include <Preferences.h>
#include <ctype.h>
#include <math.h>

#include "logging.h"
#include "compact_storage.h"

namespace {
constexpr size_t MAX_SETTINGS = 40;
constexpr char STORAGE_VERSION[] = "T1";
TofSetting settings[MAX_SETTINGS];
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
  snprintf(name, sizeof(name), "t%08X", static_cast<unsigned>(identityHash(identity)));
  return String(name);
}
bool validAddress(const String& address) {
  if (address.isEmpty() || address.length() > 192 || address[0] != '/') return false;
  for (size_t i = 0; i < address.length(); ++i) {
    const char c = address[i];
    if (isspace(static_cast<unsigned char>(c)) || c == '#' || c == '*' ||
        c == ',' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}') return false;
  }
  return true;
}
bool valid(const TofSetting& s) {
  return !s.identity.isEmpty() && !s.displayName.isEmpty() &&
         s.displayName.length() <= 64 && validAddress(s.address) &&
         s.deadband >= 1 && s.deadband <= 2000 && s.maxDistanceMm >= 31 &&
         s.maxDistanceMm <= 2000 && isfinite(s.outputMin) &&
         isfinite(s.outputMax) &&
         (s.outputType == TYPE_FLOAT || s.outputType == TYPE_INT);
}
bool same(const TofSetting& a, const TofSetting& b) {
  return a.identity == b.identity && a.displayName == b.displayName &&
         a.address == b.address && a.deadband == b.deadband &&
         a.maxDistanceMm == b.maxDistanceMm &&
         a.nearValueHigh == b.nearValueHigh &&
         fabsf(a.outputMin - b.outputMin) <= .00001f &&
         fabsf(a.outputMax - b.outputMax) <= .00001f &&
         a.outputType == b.outputType;
}
bool load(const String& identity, TofSetting& setting, bool& found) {
  found = false;
  if (compactStorageLoad(compactStorageNamespace(identity), setting)) { found = true; return true; }
  return false;

}
bool write(const TofSetting& s) {
  const String ns = compactStorageNamespace(s.identity);
  if (!compactStorageSave(ns, s)) return false;
  TofSetting verify = s; bool found = false;
  return load(s.identity, verify, found) && found && same(s, verify);
}
void saveKnown() {
  if (loadingKnown) return;
  String known;
  for (size_t i = 0; i < settingCount; ++i) { if (!known.isEmpty()) known += '\n'; known += settings[i].identity; }
  Preferences p; if (p.begin("tofcfg", false)) { p.putString("known", known); p.end(); }
}
}

void tofSettingsSetup() {
  Preferences p; String known;
  if (p.begin("tofcfg", true)) { known = p.getString("known", ""); p.end(); }
  loadingKnown = true; int offset = 0;
  while (offset < static_cast<int>(known.length())) {
    int end = known.indexOf('\n', offset); if (end < 0) end = known.length();
    const String id = known.substring(offset, end);
    if (id.startsWith("chain:") && id.length() > 6) tofSettingsEnsure(id, String("Chain ToF ") + id.substring(6));
    offset = end + 1;
  }
  loadingKnown = false;
  NANO_VERBOSE_LOGF("[ChainOSCnano][TOFCFG] setup_complete settings=%u\n", static_cast<unsigned>(settingCount));
}
TofSetting* tofSettingsEnsure(const String& identity, const String& defaultName) {
  for (size_t i = 0; i < settingCount; ++i) if (settings[i].identity == identity) return &settings[i];
  if (settingCount >= MAX_SETTINGS) return nullptr;
  TofSetting& s = settings[settingCount++]; s.identity = identity; s.displayName = defaultName;
  bool found = false; load(identity, s, found); saveKnown(); return &s;
}
size_t tofSettingsCount() { return settingCount; }
TofSetting* tofSettingsAt(size_t index) { return index < settingCount ? &settings[index] : nullptr; }
bool tofSettingsSave(const TofSetting& candidate) {
  if (!valid(candidate)) return false;
  TofSetting* destination = nullptr;
  for (size_t i = 0; i < settingCount; ++i) if (settings[i].identity == candidate.identity) destination = &settings[i];
  if (!destination || !write(candidate)) return false;
  const uint8_t mask = destination->connectedPortMask; *destination = candidate; destination->connectedPortMask = mask;
  NANO_VERBOSE_LOGF("[ChainOSCnano][TOFCFG] saved identity=%s max_mm=%d deadband=%d\n", candidate.identity.c_str(), candidate.maxDistanceMm, candidate.deadband);
  return true;
}
bool tofSettingsDelete(const String& identity) {
  size_t found = settingCount;
  for (size_t i = 0; i < settingCount; ++i) if (settings[i].identity == identity) { if (settings[i].connectedPortMask) return false; found = i; break; }
  if (found == settingCount) return false;
  compactStorageDelete(identity);
  for (size_t i = found + 1; i < settingCount; ++i) settings[i - 1] = settings[i];
  --settingCount; settings[settingCount] = TofSetting(); saveKnown(); return true;
}
void tofSettingsBeginPortUpdate(uint8_t mask) { for (size_t i = 0; i < settingCount; ++i) settings[i].connectedPortMask &= ~mask; }
void tofSettingsMarkConnected(const String& identity, uint8_t mask) { for (size_t i = 0; i < settingCount; ++i) if (settings[i].identity == identity) { settings[i].connectedPortMask |= mask; return; } }
