#include "joystick_settings.h"

#include <Preferences.h>
#include <ctype.h>
#include <math.h>

#include "logging.h"
#include "compact_storage.h"
#include "device_file_storage.h"

namespace {
constexpr size_t MAX_SETTINGS = 40;
constexpr char STORAGE_VERSION[] = "J1";
JoystickSetting settings[MAX_SETTINGS];
size_t settingCount = 0;
bool loadingKnown = false;

uint32_t hashIdentity(const String& identity) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < identity.length(); ++i) {
    hash ^= static_cast<uint8_t>(identity[i]); hash *= 16777619u;
  }
  return hash;
}
String deviceNamespace(const String& identity) {
  char name[11]; snprintf(name, sizeof(name), "j%08X", static_cast<unsigned>(hashIdentity(identity))); return String(name);
}
String indexedKey(const char* prefix, uint8_t index) { return String(prefix) + String(index); }
bool validAddress(const String& address) {
  if (address.isEmpty() || address.length() > 192 || address[0] != '/') return false;
  for (size_t i = 0; i < address.length(); ++i) {
    const char c = address[i];
    if (isspace(static_cast<unsigned char>(c)) || c == '#' || c == '*' || c == ',' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}') return false;
  }
  return true;
}
bool valid(const JoystickSetting& s) {
  if (s.identity.isEmpty() || s.displayName.isEmpty() || s.displayName.length() > 64 ||
      !validAddress(s.xAddress) || !validAddress(s.yAddress) ||
      !validAddress(s.clickSequence.address) || s.deadband < 1 || s.deadband > 254 ||
      !isfinite(s.outputMin) || !isfinite(s.outputMax) ||
      s.outputType < TYPE_FLOAT || s.outputType > TYPE_STRING ||
      s.pressMessageCount + s.releaseMessageCount > MAX_KEY_OSC_MESSAGES) return false;
  for (uint8_t i = 0; i < s.pressMessageCount; ++i) if (!validAddress(s.pressMessages[i].address)) return false;
  for (uint8_t i = 0; i < s.releaseMessageCount; ++i) if (!validAddress(s.releaseMessages[i].address)) return false;
  return true;
}
bool sameMessage(const KeyOscMessage& a, const KeyOscMessage& b) { return a.address == b.address && a.valueStr == b.valueStr && a.valueType == b.valueType; }
bool same(const JoystickSetting& a, const JoystickSetting& b) {
  if (a.identity != b.identity || a.displayName != b.displayName || a.xAddress != b.xAddress || a.yAddress != b.yAddress ||
      a.deadband != b.deadband || a.invertX != b.invertX || a.invertY != b.invertY ||
      fabsf(a.outputMin-b.outputMin) > .00001f || fabsf(a.outputMax-b.outputMax) > .00001f || a.outputType != b.outputType ||
      a.clickMode != b.clickMode || a.pressMessageCount != b.pressMessageCount || a.releaseMessageCount != b.releaseMessageCount ||
      a.clickSequence.address != b.clickSequence.address || a.clickSequence.valueType != b.clickSequence.valueType ||
      fabsf(a.clickSequence.start-b.clickSequence.start) > .00001f || fabsf(a.clickSequence.end-b.clickSequence.end) > .00001f || fabsf(a.clickSequence.step-b.clickSequence.step) > .00001f) return false;
  for (uint8_t i=0;i<a.pressMessageCount;++i) if(!sameMessage(a.pressMessages[i],b.pressMessages[i])) return false;
  for (uint8_t i=0;i<a.releaseMessageCount;++i) if(!sameMessage(a.releaseMessages[i],b.releaseMessages[i])) return false;
  return true;
}
bool load(const String& identity, JoystickSetting& setting, bool& found) {
  found=false; const DeviceFileLoadResult result=deviceFileStorageLoad(setting);
  if(result==DeviceFileLoadResult::Loaded){found=true;return true;}
  if(result==DeviceFileLoadResult::Error){found=true;return false;}
  if(compactStorageLoad(compactStorageNamespace(identity),setting)){found=true;if(deviceFileStorageSave(setting))NANO_STORAGE_LOGF("[ChainOSCnano][JOYCFG] migrated identity=%s source=NVS target=LittleFS\n",identity.c_str());return true;}
  return false;

}
bool write(const JoystickSetting& s){if(!deviceFileStorageSave(s))return false;JoystickSetting v=s;bool found=false;return load(s.identity,v,found)&&found&&same(s,v);}
void saveKnown(){/* LittleFS files are the catalog; NVS is migration-only. */}
}
void joystickSettingsSetup(){deviceFileStorageBegin();String fileIdentities[MAX_SETTINGS];size_t fileCount=deviceFileStorageList("joystick",fileIdentities,MAX_SETTINGS);loadingKnown=true;for(size_t i=0;i<fileCount;++i)if(fileIdentities[i].startsWith("chain:")&&fileIdentities[i].length()>6)joystickSettingsEnsure(fileIdentities[i],String("Chain Joystick ")+fileIdentities[i].substring(6));loadingKnown=false;Preferences p;String known;if(p.begin("joycfg",true)){known=p.getString("known","");p.end();}loadingKnown=true;int offset=0;while(offset<(int)known.length()){int end=known.indexOf('\n',offset);if(end<0)end=known.length();String id=known.substring(offset,end);if(id.startsWith("chain:")&&id.length()>6)joystickSettingsEnsure(id,String("Chain Joystick ")+id.substring(6));offset=end+1;}loadingKnown=false;NANO_VERBOSE_LOGF("[ChainOSCnano][JOYCFG] setup_complete settings=%u\n",(unsigned)settingCount);}
JoystickSetting* joystickSettingsEnsure(const String& identity,const String& defaultName){for(size_t i=0;i<settingCount;++i)if(settings[i].identity==identity)return &settings[i];if(settingCount>=MAX_SETTINGS)return nullptr;JoystickSetting& s=settings[settingCount++];s.identity=identity;s.displayName=defaultName;s.pressMessages[0].address="/avatar/parameters/JoyClick";s.pressMessages[0].valueStr="1.0";s.pressMessages[0].valueType=TYPE_FLOAT;s.releaseMessages[0].address="/avatar/parameters/JoyClick";s.releaseMessages[0].valueStr="0.0";s.releaseMessages[0].valueType=TYPE_FLOAT;s.clickSequence.address="/avatar/parameters/JoySeq";bool found=false;load(identity,s,found);saveKnown();return &s;}
size_t joystickSettingsCount(){return settingCount;} JoystickSetting* joystickSettingsAt(size_t i){return i<settingCount?&settings[i]:nullptr;}
bool joystickSettingsSave(const JoystickSetting& c){if(!valid(c))return false;JoystickSetting* d=nullptr;for(size_t i=0;i<settingCount;++i)if(settings[i].identity==c.identity)d=&settings[i];if(!d||!write(c))return false;uint8_t mask=d->connectedPortMask;*d=c;d->connectedPortMask=mask;keySettingsNormalizeSequence(d->clickSequence);return true;}
bool joystickSettingsDelete(const String& identity){size_t found=settingCount;for(size_t i=0;i<settingCount;++i)if(settings[i].identity==identity){if(settings[i].connectedPortMask)return false;found=i;break;}if(found==settingCount||!deviceFileStorageRemove("joystick",identity))return false;compactStorageDelete(identity);for(size_t i=found+1;i<settingCount;++i)settings[i-1]=settings[i];--settingCount;settings[settingCount]=JoystickSetting();saveKnown();return true;}
void joystickSettingsBeginPortUpdate(uint8_t mask){for(size_t i=0;i<settingCount;++i)settings[i].connectedPortMask&=~mask;}void joystickSettingsMarkConnected(const String& identity,uint8_t mask){for(size_t i=0;i<settingCount;++i)if(settings[i].identity==identity){settings[i].connectedPortMask|=mask;return;}}
