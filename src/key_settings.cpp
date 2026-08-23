#include "key_settings.h"
#include <Preferences.h>
#include <ctype.h>
#include <math.h>
#include "compact_storage.h"
#include "logging.h"

namespace {
constexpr size_t MAX_KEY_SETTINGS = 40;
KeySetting settings[MAX_KEY_SETTINGS]; size_t settingCount=0; bool loadingKnown=false;
bool validAddress(const String& a){if(a.isEmpty()||a.length()>192||a[0]!='/')return false;for(size_t i=0;i<a.length();++i){char c=a[i];if(isspace((unsigned char)c)||c=='#'||c=='*'||c==','||c=='?'||c=='['||c==']'||c=='{'||c=='}')return false;}return true;}
bool sameMessage(const KeyOscMessage&a,const KeyOscMessage&b){return a.address==b.address&&a.valueStr==b.valueStr&&a.valueType==b.valueType;}
bool sameSetting(const KeySetting&a,const KeySetting&b){
  const char* reason=nullptr;int messageIndex=-1;
  if(a.identity!=b.identity)reason="identity";
  else if(a.displayName!=b.displayName)reason="display_name";
  else if(a.mode!=b.mode)reason="mode";
  else if(a.pressMessageCount!=b.pressMessageCount)reason="press_count";
  else if(a.releaseMessageCount!=b.releaseMessageCount)reason="release_count";
  else if(a.sequence.address!=b.sequence.address)reason="sequence_address";
  else if(a.sequence.valueType!=b.sequence.valueType)reason="sequence_type";
  else if(fabsf(a.sequence.start-b.sequence.start)>.00001f)reason="sequence_start";
  else if(fabsf(a.sequence.end-b.sequence.end)>.00001f)reason="sequence_end";
  else if(fabsf(a.sequence.step-b.sequence.step)>.00001f)reason="sequence_step";
  if(!reason)for(uint8_t i=0;i<a.pressMessageCount;++i)if(!sameMessage(a.pressMessages[i],b.pressMessages[i])){reason="press_message";messageIndex=i;break;}
  if(!reason)for(uint8_t i=0;i<a.releaseMessageCount;++i)if(!sameMessage(a.releaseMessages[i],b.releaseMessages[i])){reason="release_message";messageIndex=i;break;}
  if(reason){
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] key_verify uid=%s result=mismatch field=%s index=%d mode=%d/%d press=%u/%u release=%u/%u seq_type=%d/%d seq=%.7f,%.7f,%.7f/%.7f,%.7f,%.7f\n",
      a.identity.c_str(),reason,messageIndex,(int)a.mode,(int)b.mode,
      (unsigned)a.pressMessageCount,(unsigned)b.pressMessageCount,
      (unsigned)a.releaseMessageCount,(unsigned)b.releaseMessageCount,
      (int)a.sequence.valueType,(int)b.sequence.valueType,
      a.sequence.start,a.sequence.end,a.sequence.step,
      b.sequence.start,b.sequence.end,b.sequence.step);
    return false;
  }
  NANO_STORAGE_LOGF("[ChainOSCnano][NVS] key_verify uid=%s result=ok\n",a.identity.c_str());
  return true;
}
bool loadSetting(KeySetting&s){return compactStorageLoad(compactStorageNamespace(s.identity),s);}
bool writeSetting(const KeySetting&s){String ns=compactStorageNamespace(s.identity);if(!compactStorageSave(ns,s))return false;KeySetting v=s;if(!compactStorageLoad(ns,v)){NANO_STORAGE_LOGF("[ChainOSCnano][NVS] key_verify uid=%s result=decode_failed\n",s.identity.c_str());return false;}return sameSetting(s,v);}
void saveKnown(){if(loadingKnown)return;String known;for(size_t i=0;i<settingCount;++i){if(settings[i].builtIn)continue;if(!known.isEmpty())known+='\n';known+=settings[i].identity;}Preferences p;if(p.begin("keycfg",false)){p.putString("known",known);p.end();}}
}

void keySettingsNormalizeSequence(KeySequenceConfig&s){if(!isfinite(s.start))s.start=0;if(!isfinite(s.end))s.end=10;if(!isfinite(s.step)||fabsf(s.step)<1e-9f)s.step=1;if(s.start<=s.end&&s.step<0)s.step=-s.step;if(s.start>s.end&&s.step>0)s.step=-s.step;s.current=s.start;}
void keySettingsSetup(){Preferences p;String known;if(p.begin("keycfg",true)){known=p.getString("known","");p.end();}loadingKnown=true;int o=0;while(o<(int)known.length()){int e=known.indexOf('\n',o);if(e<0)e=known.length();String id=known.substring(o,e);if(id.startsWith("chain:")&&id.length()>6){String uid=id.substring(6);keySettingsEnsure(id,String("Chain Key ")+uid,String("/chainoscnano/chain/key/")+uid);}o=e+1;}loadingKnown=false;}
KeySetting* keySettingsEnsure(const String&id,const String&name,const String&address){for(size_t i=0;i<settingCount;++i)if(settings[i].identity==id)return &settings[i];if(settingCount>=MAX_KEY_SETTINGS)return nullptr;KeySetting&s=settings[settingCount++];s.identity=id;s.displayName=name;s.pressMessages[0].address=address;s.pressMessages[0].valueType=TYPE_INT;s.pressMessages[0].valueStr="1";s.releaseMessages[0].address=address;s.releaseMessages[0].valueType=TYPE_INT;s.releaseMessages[0].valueStr="0";s.sequence.address=address;keySettingsNormalizeSequence(s.sequence);loadSetting(s);if(!s.builtIn)saveKnown();return &s;}
size_t keySettingsCount(){return settingCount;} KeySetting* keySettingsAt(size_t i){return i<settingCount?&settings[i]:nullptr;}
bool keySettingsSave(const KeySetting&c){if(c.identity.isEmpty()||c.displayName.isEmpty()||c.displayName.length()>64||c.pressMessageCount+c.releaseMessageCount>MAX_KEY_OSC_MESSAGES||!validAddress(c.sequence.address))return false;for(uint8_t i=0;i<c.pressMessageCount;++i)if(!validAddress(c.pressMessages[i].address))return false;for(uint8_t i=0;i<c.releaseMessageCount;++i)if(!validAddress(c.releaseMessages[i].address))return false;KeySetting*d=nullptr;for(size_t i=0;i<settingCount;++i)if(settings[i].identity==c.identity)d=&settings[i];if(!d||!writeSetting(c))return false;bool builtIn=d->builtIn;uint8_t mask=d->connectedPortMask;*d=c;d->builtIn=builtIn;d->connectedPortMask=mask;keySettingsNormalizeSequence(d->sequence);return true;}
bool keySettingsDelete(const String&id){size_t found=settingCount;for(size_t i=0;i<settingCount;++i)if(settings[i].identity==id){if(settings[i].builtIn||settings[i].connectedPortMask)return false;found=i;break;}if(found==settingCount)return false;compactStorageDelete(id);for(size_t i=found+1;i<settingCount;++i)settings[i-1]=settings[i];--settingCount;settings[settingCount]=KeySetting();saveKnown();return true;}
void keySettingsBeginPortUpdate(uint8_t mask){for(size_t i=0;i<settingCount;++i)if(!settings[i].builtIn)settings[i].connectedPortMask&=~mask;}
void keySettingsMarkConnected(const String&id,uint8_t mask){for(size_t i=0;i<settingCount;++i)if(settings[i].identity==id){settings[i].connectedPortMask|=mask;return;}}
void keySettingsPrintState(){}
