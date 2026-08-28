#include "compact_storage.h"
#include "logging.h"

#include <Preferences.h>
#include <ctype.h>
#include <math.h>
#include <nvs.h>

namespace {
constexpr char FS = '\x1f';
constexpr char MARKER[] = "C1";
constexpr size_t MAX_COMPACT_BYTES = 4096;

void add(String& out, String value) {
  value.replace(String(FS), " ");
  if (!out.isEmpty()) out += FS;
  out += value;
}
void add(String& out, int value) { add(out, String(value)); }
void add(String& out, float value) { add(out, String(value, 7)); }

String take(const String& in, int& offset) {
  if (offset > static_cast<int>(in.length())) return String();
  int end = in.indexOf(FS, offset);
  if (end < 0) end = in.length();
  String value = in.substring(offset, end);
  offset = end + 1;
  return value;
}

ValueType takeValueType(const String& in, int& offset,
                        ValueType maximum = TYPE_STRING) {
  // Arduino's constrain() is a macro. Passing take() directly to it can
  // evaluate take() more than once and consume multiple serialized fields.
  const int raw = take(in, offset).toInt();
  return static_cast<ValueType>(constrain(
      raw, static_cast<int>(TYPE_FLOAT), static_cast<int>(maximum)));
}

bool validAddress(const String& address) {
  if (address.isEmpty() || address.length() > 192 || address[0] != '/') return false;
  for (size_t i = 0; i < address.length(); ++i) {
    const char c = address[i];
    if (isspace(static_cast<unsigned char>(c)) || c == '#' || c == '*' ||
        c == ',' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}')
      return false;
  }
  return true;
}

void addMessages(String& out, const KeyOscMessage* press, uint8_t pressCount,
                 const KeyOscMessage* release, uint8_t releaseCount) {
  add(out, static_cast<int>(pressCount));
  for (uint8_t i = 0; i < pressCount; ++i) {
    add(out, press[i].address); add(out, press[i].valueStr);
    add(out, static_cast<int>(press[i].valueType));
  }
  add(out, static_cast<int>(releaseCount));
  for (uint8_t i = 0; i < releaseCount; ++i) {
    add(out, release[i].address); add(out, release[i].valueStr);
    add(out, static_cast<int>(release[i].valueType));
  }
}

bool takeMessages(const String& in, int& offset, KeyOscMessage* press,
                  uint8_t& pressCount, KeyOscMessage* release,
                  uint8_t& releaseCount) {
  const int messagesOffset = offset;
  int pc = take(in, offset).toInt();
  NANO_STORAGE_LOGF("[ChainOSCnano][NVS] decode_messages begin_offset=%d after_press_count=%d press=%d bytes=%u\n",
                    messagesOffset, offset, pc, static_cast<unsigned>(in.length()));
  if (pc < 0 || pc > MAX_KEY_OSC_MESSAGES) {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] decode_messages result=invalid_press_count count=%d offset=%d bytes=%u\n",
                      pc, offset, static_cast<unsigned>(in.length()));
    return false;
  }
  pressCount = static_cast<uint8_t>(pc);
  for (int i = 0; i < pc; ++i) {
    press[i].address = take(in, offset); press[i].valueStr = take(in, offset);
    press[i].valueType = takeValueType(in, offset);
    if (!validAddress(press[i].address)) {
      NANO_STORAGE_LOGF("[ChainOSCnano][NVS] decode_messages result=invalid_press_address index=%d address_bytes=%u offset=%d\n",
                        i, static_cast<unsigned>(press[i].address.length()), offset);
      return false;
    }
  }
  int rc = take(in, offset).toInt();
  if (rc < 0 || pc + rc > MAX_KEY_OSC_MESSAGES) {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] decode_messages result=invalid_release_count press=%d release=%d offset=%d bytes=%u\n",
                      pc, rc, offset, static_cast<unsigned>(in.length()));
    return false;
  }
  releaseCount = static_cast<uint8_t>(rc);
  for (int i = 0; i < rc; ++i) {
    release[i].address = take(in, offset); release[i].valueStr = take(in, offset);
    release[i].valueType = takeValueType(in, offset);
    if (!validAddress(release[i].address)) {
      NANO_STORAGE_LOGF("[ChainOSCnano][NVS] decode_messages result=invalid_release_address index=%d address_bytes=%u offset=%d\n",
                        i, static_cast<unsigned>(release[i].address.length()), offset);
      return false;
    }
  }
  NANO_STORAGE_LOGF("[ChainOSCnano][NVS] decode_messages result=ok press=%d release=%d offset=%d bytes=%u\n",
                    pc, rc, offset, static_cast<unsigned>(in.length()));
  return true;
}

void logNvsStats(const char* phase) {
  nvs_stats_t stats{};
  const esp_err_t result = nvs_get_stats(nullptr, &stats);
  if (result == ESP_OK) {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] stats phase=%s used=%lu free=%lu total=%lu namespaces=%lu\n",
                      phase, static_cast<unsigned long>(stats.used_entries),
                      static_cast<unsigned long>(stats.free_entries),
                      static_cast<unsigned long>(stats.total_entries),
                      static_cast<unsigned long>(stats.namespace_count));
  } else {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] stats phase=%s error=%d\n", phase,
                      static_cast<int>(result));
  }
}

void logNamespaceUsage(const char* type, const String& identity,
                       const String& nameSpace, size_t serializedBytes) {
#if CHAINOSCNANO_STORAGE_DEBUG
  constexpr size_t NVS_ENTRY_BYTES = 32;
  nvs_handle_t handle = 0;
  const esp_err_t openResult =
      nvs_open(nameSpace.c_str(), NVS_READONLY, &handle);
  if (openResult != ESP_OK) {
    NANO_STORAGE_LOGF(
        "[ChainOSCnano][STORAGE] type=%s uid=%s ns=%s "
        "serialized_bytes=%u result=open_failed error=%d\n",
        type, identity.c_str(), nameSpace.c_str(),
        static_cast<unsigned>(serializedBytes),
        static_cast<int>(openResult));
    return;
  }
  size_t entries = 0;
  const esp_err_t countResult = nvs_get_used_entry_count(handle, &entries);
  nvs_close(handle);
  if (countResult != ESP_OK) {
    NANO_STORAGE_LOGF(
        "[ChainOSCnano][STORAGE] type=%s uid=%s ns=%s "
        "serialized_bytes=%u result=count_failed error=%d\n",
        type, identity.c_str(), nameSpace.c_str(),
        static_cast<unsigned>(serializedBytes),
        static_cast<int>(countResult));
    return;
  }
  const size_t namespaceEntries = entries + 1;
  NANO_STORAGE_LOGF(
      "[ChainOSCnano][STORAGE] type=%s uid=%s ns=%s "
      "serialized_bytes=%u data_entries=%u namespace_entries=%u "
      "entry_bytes_estimate=%u\n",
      type, identity.c_str(), nameSpace.c_str(),
      static_cast<unsigned>(serializedBytes),
      static_cast<unsigned>(entries),
      static_cast<unsigned>(namespaceEntries),
      static_cast<unsigned>(namespaceEntries * NVS_ENTRY_BYTES));
#else
  (void)type;
  (void)identity;
  (void)nameSpace;
  (void)serializedBytes;
#endif
}

bool readBlob(const String& nameSpace, String& blob) {
  Preferences p;
  if (!p.begin(nameSpace.c_str(), true)) {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] read ns=%s result=begin_failed\n",
                      nameSpace.c_str());
    return false;
  }
  blob = p.getString("cfg", "");
  p.end();
  const bool ok = !blob.isEmpty() && blob.length() <= MAX_COMPACT_BYTES;
  NANO_STORAGE_LOGF("[ChainOSCnano][NVS] read ns=%s bytes=%u result=%s\n",
                    nameSpace.c_str(), static_cast<unsigned>(blob.length()),
                    ok ? "ok" : "invalid_or_missing");
  return ok;
}

bool writeBlob(const String& nameSpace, const char* type,
               const String& identity, const String& blob) {
  NANO_STORAGE_LOGF("[ChainOSCnano][NVS] save_begin type=%s uid=%s ns=%s bytes=%u heap=%u\n",
                    type, identity.c_str(), nameSpace.c_str(),
                    static_cast<unsigned>(blob.length()),
                    static_cast<unsigned>(ESP.getFreeHeap()));
  logNvsStats("before_save");
  if (blob.isEmpty() || blob.length() > MAX_COMPACT_BYTES) {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] save_end type=%s uid=%s result=invalid_size\n",
                      type, identity.c_str());
    return false;
  }
  Preferences p;
  if (!p.begin(nameSpace.c_str(), false)) {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] save_end type=%s uid=%s result=begin_failed\n",
                      type, identity.c_str());
    logNvsStats("begin_failed");
    return false;
  }
  const bool cleared = p.clear();
  const size_t written = p.putString("cfg", blob);
  p.end();
  NANO_STORAGE_LOGF("[ChainOSCnano][NVS] write type=%s uid=%s clear=%d requested=%u written=%u\n",
                    type, identity.c_str(), cleared ? 1 : 0,
                    static_cast<unsigned>(blob.length()),
                    static_cast<unsigned>(written));
  if (written != blob.length()) {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] save_end type=%s uid=%s result=short_write\n",
                      type, identity.c_str());
    logNvsStats("short_write");
    return false;
  }
  if (!p.begin(nameSpace.c_str(), true)) {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] save_end type=%s uid=%s result=verify_begin_failed\n",
                      type, identity.c_str());
    logNvsStats("verify_begin_failed");
    return false;
  }
  const String verify = p.getString("cfg", "");
  p.end();
  const bool matches = verify == blob;
  NANO_STORAGE_LOGF("[ChainOSCnano][NVS] save_end type=%s uid=%s verify_bytes=%u result=%s heap=%u\n",
                    type, identity.c_str(), static_cast<unsigned>(verify.length()),
                    matches ? "ok" : "verify_mismatch",
                    static_cast<unsigned>(ESP.getFreeHeap()));
  logNvsStats(matches ? "after_save" : "verify_mismatch");
  if (matches) logNamespaceUsage(type, identity, nameSpace, blob.length());
  return matches;
}

bool header(const String& blob, int& offset, const char* type,
            const String& identity, String& displayName) {
  if (take(blob, offset) != MARKER || take(blob, offset) != type ||
      take(blob, offset) != identity) return false;
  displayName = take(blob, offset);
  return !displayName.isEmpty() && displayName.length() <= 64;
}

void begin(String& blob, const char* type, const String& identity,
           const String& displayName) {
  blob.reserve(384); add(blob, String(MARKER)); add(blob, String(type));
  add(blob, identity); add(blob, displayName);
}
}  // namespace

String compactStorageNamespace(const String& identity) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < identity.length(); ++i) {
    hash ^= static_cast<uint8_t>(identity[i]);
    hash *= 16777619u;
  }
  char name[11];
  snprintf(name, sizeof(name), "c%08X", static_cast<unsigned>(hash));
  return String(name);
}

void compactStorageDelete(const String& identity) {
  Preferences p;
  const String ns = compactStorageNamespace(identity);
  if (p.begin(ns.c_str(), false)) { p.clear(); p.end(); }
}

bool compactStorageSave(const String& ns, const KeySetting& s) {
  String b; begin(b, "K", s.identity, s.displayName); add(b, (int)s.mode);
  add(b, s.sequence.address); add(b, (int)s.sequence.valueType);
  add(b, s.sequence.start); add(b, s.sequence.end); add(b, s.sequence.step);
  addMessages(b, s.pressMessages, s.pressMessageCount, s.releaseMessages, s.releaseMessageCount);
  return writeBlob(ns, "Key", s.identity, b);
}
bool compactStorageLoad(const String& ns, KeySetting& s) {
  String b; if (!readBlob(ns, b)) return false; int o=0; KeySetting c=s;
  int separators = 0;
  for (size_t i = 0; i < b.length(); ++i) if (b[i] == FS) ++separators;
  NANO_STORAGE_LOGF("[ChainOSCnano][NVS] key_decode uid=%s bytes=%u separators=%d\n",
                    s.identity.c_str(), static_cast<unsigned>(b.length()), separators);
  if(!header(b,o,"K",s.identity,c.displayName)){
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] key_decode uid=%s result=invalid_header offset=%d bytes=%u\n",
                      s.identity.c_str(),o,static_cast<unsigned>(b.length()));
    return false;
  }
  const int afterHeader=o;
  String modeToken=take(b,o);
  const int afterMode=o;
  c.mode=modeToken.toInt()==MODE_SEQUENCE?MODE_SEQUENCE:MODE_PRESS_RELEASE;
  c.sequence.address=take(b,o);
  const int afterSequenceAddress=o;
  String sequenceTypeToken=take(b,o);
  c.sequence.valueType=static_cast<ValueType>(constrain(sequenceTypeToken.toInt(),0,2));
  const int afterSequenceType=o;
  String sequenceStartToken=take(b,o); c.sequence.start=sequenceStartToken.toFloat();
  const int afterSequenceStart=o;
  String sequenceEndToken=take(b,o); c.sequence.end=sequenceEndToken.toFloat();
  const int afterSequenceEnd=o;
  String sequenceStepToken=take(b,o); c.sequence.step=sequenceStepToken.toFloat();
  NANO_STORAGE_LOGF("[ChainOSCnano][NVS] key_decode_offsets header=%d mode=%d seq_address=%d seq_type=%d seq_start=%d seq_end=%d seq_step=%d token_lengths=%u,%u,%u,%u,%u,%u\n",
                    afterHeader,afterMode,afterSequenceAddress,afterSequenceType,
                    afterSequenceStart,afterSequenceEnd,o,
                    static_cast<unsigned>(modeToken.length()),
                    static_cast<unsigned>(c.sequence.address.length()),
                    static_cast<unsigned>(sequenceTypeToken.length()),
                    static_cast<unsigned>(sequenceStartToken.length()),
                    static_cast<unsigned>(sequenceEndToken.length()),
                    static_cast<unsigned>(sequenceStepToken.length()));
  if(!validAddress(c.sequence.address)){
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] key_decode uid=%s result=invalid_sequence_address address_bytes=%u offset=%d\n",
                      s.identity.c_str(),static_cast<unsigned>(c.sequence.address.length()),o);
    return false;
  }
  if(!takeMessages(b,o,c.pressMessages,c.pressMessageCount,c.releaseMessages,c.releaseMessageCount)){
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] key_decode uid=%s result=invalid_messages offset=%d bytes=%u\n",
                      s.identity.c_str(),o,static_cast<unsigned>(b.length()));
    return false;
  }
  if(o < static_cast<int>(b.length())) {
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] key_decode uid=%s result=trailing_data offset=%d bytes=%u\n",
                      s.identity.c_str(),o,static_cast<unsigned>(b.length()));
  }
  keySettingsNormalizeSequence(c.sequence); s=c; return true;
}

bool compactStorageSave(const String& ns, const EncoderSetting& s) {
  String b;begin(b,"E",s.identity,s.displayName);add(b,s.rotationAddress);add(b,s.sendIncrement?1:0);
  add(b,s.absoluteInputMin);add(b,s.absoluteInputMax);add(b,s.incrementScale);add(b,s.outputMin);add(b,s.outputMax);add(b,(int)s.outputType);
  add(b,(int)s.clickMode);add(b,s.clickSequence.address);add(b,(int)s.clickSequence.valueType);add(b,s.clickSequence.start);add(b,s.clickSequence.end);add(b,s.clickSequence.step);
  addMessages(b,s.pressMessages,s.pressMessageCount,s.releaseMessages,s.releaseMessageCount);return writeBlob(ns,"Encoder",s.identity,b);
}
bool compactStorageLoad(const String& ns, EncoderSetting& s) {
  String b;if(!readBlob(ns,b))return false;int o=0;EncoderSetting c=s;if(!header(b,o,"E",s.identity,c.displayName))return false;
  c.rotationAddress=take(b,o);c.sendIncrement=take(b,o).toInt()!=0;c.absoluteInputMin=take(b,o).toFloat();c.absoluteInputMax=take(b,o).toFloat();c.incrementScale=take(b,o).toFloat();c.outputMin=take(b,o).toFloat();c.outputMax=take(b,o).toFloat();c.outputType=takeValueType(b,o);
  c.clickMode=take(b,o).toInt()==MODE_SEQUENCE?MODE_SEQUENCE:MODE_PRESS_RELEASE;c.clickSequence.address=take(b,o);c.clickSequence.valueType=takeValueType(b,o);c.clickSequence.start=take(b,o).toFloat();c.clickSequence.end=take(b,o).toFloat();c.clickSequence.step=take(b,o).toFloat();
  if(!validAddress(c.rotationAddress)||!validAddress(c.clickSequence.address)||!takeMessages(b,o,c.pressMessages,c.pressMessageCount,c.releaseMessages,c.releaseMessageCount))return false;keySettingsNormalizeSequence(c.clickSequence);s=c;return true;
}

bool compactStorageSave(const String& ns,const AngleSetting& s){String b;begin(b,"A",s.identity,s.displayName);add(b,s.address);add(b,s.use12Bit?1:0);add(b,s.deadband);add(b,s.outputMin);add(b,s.outputMax);add(b,(int)s.outputType);return writeBlob(ns,"Angle",s.identity,b);}
bool compactStorageLoad(const String& ns,AngleSetting& s){String b;if(!readBlob(ns,b))return false;int o=0;AngleSetting c=s;if(!header(b,o,"A",s.identity,c.displayName))return false;c.address=take(b,o);c.use12Bit=take(b,o).toInt()!=0;c.deadband=take(b,o).toInt();c.outputMin=take(b,o).toFloat();c.outputMax=take(b,o).toFloat();c.outputType=takeValueType(b,o);if(!validAddress(c.address)||c.deadband<1||!isfinite(c.outputMin)||!isfinite(c.outputMax))return false;s=c;return true;}

bool compactStorageSave(const String& ns,const TofSetting& s){String b;begin(b,"T",s.identity,s.displayName);add(b,s.address);add(b,s.deadband);add(b,s.maxDistanceMm);add(b,s.nearValueHigh?1:0);add(b,s.outputMin);add(b,s.outputMax);add(b,(int)s.outputType);return writeBlob(ns,"ToF",s.identity,b);}
bool compactStorageLoad(const String& ns,TofSetting& s){String b;if(!readBlob(ns,b))return false;int o=0;TofSetting c=s;if(!header(b,o,"T",s.identity,c.displayName))return false;c.address=take(b,o);c.deadband=take(b,o).toInt();c.maxDistanceMm=take(b,o).toInt();c.nearValueHigh=take(b,o).toInt()!=0;c.outputMin=take(b,o).toFloat();c.outputMax=take(b,o).toFloat();c.outputType=takeValueType(b,o,TYPE_INT);if(!validAddress(c.address)||c.deadband<1||c.deadband>2000||c.maxDistanceMm<31||c.maxDistanceMm>2000||!isfinite(c.outputMin)||!isfinite(c.outputMax))return false;s=c;return true;}

bool compactStorageSave(const String& ns,const JoystickSetting& s){String b;begin(b,"J",s.identity,s.displayName);add(b,s.xAddress);add(b,s.yAddress);add(b,s.deadband);add(b,s.invertX?1:0);add(b,s.invertY?1:0);add(b,s.outputMin);add(b,s.outputMax);add(b,(int)s.outputType);add(b,(int)s.clickMode);add(b,s.clickSequence.address);add(b,(int)s.clickSequence.valueType);add(b,s.clickSequence.start);add(b,s.clickSequence.end);add(b,s.clickSequence.step);addMessages(b,s.pressMessages,s.pressMessageCount,s.releaseMessages,s.releaseMessageCount);return writeBlob(ns,"Joystick",s.identity,b);}
bool compactStorageLoad(const String& ns,JoystickSetting& s){String b;if(!readBlob(ns,b))return false;int o=0;JoystickSetting c=s;if(!header(b,o,"J",s.identity,c.displayName))return false;c.xAddress=take(b,o);c.yAddress=take(b,o);c.deadband=take(b,o).toInt();c.invertX=take(b,o).toInt()!=0;c.invertY=take(b,o).toInt()!=0;c.outputMin=take(b,o).toFloat();c.outputMax=take(b,o).toFloat();c.outputType=takeValueType(b,o);c.clickMode=take(b,o).toInt()==MODE_SEQUENCE?MODE_SEQUENCE:MODE_PRESS_RELEASE;c.clickSequence.address=take(b,o);c.clickSequence.valueType=takeValueType(b,o);c.clickSequence.start=take(b,o).toFloat();c.clickSequence.end=take(b,o).toFloat();c.clickSequence.step=take(b,o).toFloat();if(!validAddress(c.xAddress)||!validAddress(c.yAddress)||!validAddress(c.clickSequence.address)||c.deadband<1||c.deadband>254||!takeMessages(b,o,c.pressMessages,c.pressMessageCount,c.releaseMessages,c.releaseMessageCount))return false;keySettingsNormalizeSequence(c.clickSequence);s=c;return true;}
