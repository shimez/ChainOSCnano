#include "network_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <ctype.h>
#include <errno.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>

#include "nano_hardware.h"
#include <limits.h>
#include <math.h>

#include "config.h"
#include "angle_settings.h"
#include "chain_probe.h"
#include "encoder_settings.h"
#include "key_settings.h"
#include "joystick_settings.h"
#include "logging.h"
#include "osc_manager.h"
#include "responsive_web_server.h"
#include "tof_settings.h"
#include "system_settings.h"

namespace {

enum class NetworkState : uint8_t {
  CONNECTING,
  CONNECTED,
  AP_MODE,
};

ResponsiveWebServer server(80);
DNSServer dnsServer;
NetworkState networkState = NetworkState::CONNECTING;
unsigned long restartAtMs = 0;
bool restartScheduled = false;
bool mdnsRunning = false;
bool routesRegistered = false;
bool webServerStarted = false;
String savedSsid;
String savedPassword;
enum class UiLanguage : uint8_t { ENGLISH = 0, JAPANESE = 1 };
UiLanguage uiLanguage = UiLanguage::ENGLISH;
bool uiLanguageConfigured = false;

constexpr char SETTINGS_FORMAT_NAME[] = "ChainOSCnano-settings";
constexpr int SETTINGS_SCHEMA_VERSION = 1;
constexpr char DEVICE_PRESET_FORMAT_NAME[] = "ChainOSC-device-preset";
constexpr char LEGACY_DEVICE_PRESET_FORMAT_NAME[] = "M5ChainOSC-device-preset";
constexpr int DEVICE_PRESET_SCHEMA_VERSION = 1;
constexpr int CHAIN_KEY_DEVICE_TYPE = 3;
constexpr int CHAIN_ENCODER_DEVICE_TYPE = 1;
constexpr int CHAIN_ANGLE_DEVICE_TYPE = 2;
constexpr int CHAIN_TOF_DEVICE_TYPE = 5;
constexpr int CHAIN_JOYSTICK_DEVICE_TYPE = 4;
constexpr size_t MAX_IMPORT_BYTES = 32768;
constexpr size_t MAX_PRESET_BYTES = 16384;

#if CHAINOSCNANO_WEB_PERF_DEBUG
uint32_t webRequestSequence = 0;
void logWebPerf(uint32_t requestId, uint32_t startedMs, const char* phase,
                size_t bytes, uint32_t operationMs, size_t cardCount) {
  NANO_VERBOSE_LOGF(
      "[ChainOSCnano][WEBPERF] req=%lu phase=%s elapsed=%lu op=%lu "
      "bytes=%u cards=%u free=%u min=%u largest=%u connected=%u\n",
      static_cast<unsigned long>(requestId), phase,
      static_cast<unsigned long>(millis() - startedMs),
      static_cast<unsigned long>(operationMs), static_cast<unsigned>(bytes),
      static_cast<unsigned>(cardCount),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
      server.client().connected() ? 1U : 0U);
}
#endif

bool isJapaneseUi() { return uiLanguage == UiLanguage::JAPANESE; }
const char* tr(const char* english, const char* japanese) {
  return isJapaneseUi() ? japanese : english;
}

void saveUiLanguage() {
  if (systemSettingsSaveUiLanguage(static_cast<uint8_t>(uiLanguage))) {
    uiLanguageConfigured = true;
  }
}

void applyBrowserLanguageOnFirstVisit() {
  if (uiLanguageConfigured) return;
  String accepted = server.header("Accept-Language");
  accepted.toLowerCase();
  if (!accepted.isEmpty()) {
    uiLanguage = accepted.startsWith("ja") ? UiLanguage::JAPANESE
                                            : UiLanguage::ENGLISH;
    saveUiLanguage();
  }
}

const char PAGE_STYLE[] PROGMEM = R"CSS(
body{font-family:sans-serif;margin:16px;background:#f5f5f5;color:#18212f}
main{max-width:1100px;margin:0 auto}
.card{background:#fff;padding:16px;border-radius:10px;margin-bottom:16px;box-shadow:0 2px 5px rgba(0,0,0,.1)}
h1{font-size:1.4em;margin:0 0 16px}h2{margin:0;font-size:1.1em}
label{display:block;margin-top:10px;font-weight:bold;font-size:.9em}
input,select{width:100%;padding:8px;margin-top:4px;box-sizing:border-box;border:1px solid #9aa3ad;border-radius:2px;font-size:1em}
input.invalid,select.invalid{border:2px solid #c73c4a;background:#fff8f8}.osc-row small,.address-field small{display:flex;justify-content:space-between;min-height:17px;color:#697586}.osc-row .err,.address-field .err{color:#c73c4a}
button{width:100%;padding:12px;background:#28a745;color:#fff;border:none;border-radius:6px;font-size:16px;margin-top:12px;cursor:pointer}
.primary{background:#3267e3}.danger{background:#dc3545}.note{color:#888;font-size:.9em;line-height:1.5}.meta{color:#666;font-size:.85em}
.status{padding:10px 12px;background:#edf3ff;color:#244da7;border:1px solid #cddbf8;border-radius:8px}
.system-grid,.key-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px;align-items:start}.system-grid label,.key-grid label{margin-top:0}
.system-item{padding:10px;background:#f8f9fa;border-radius:6px}.system-item strong{display:block;margin-bottom:5px;font-size:.9em}.system-item code{word-break:break-all}
.section-title{margin:24px 2px 10px}.saved-settings{margin-top:28px}
.device{position:relative;border-left:5px solid #6f42c1}.device-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:10px}.device-head h2{display:flex;align-items:center;gap:4px;flex-wrap:wrap}
.collapse-button{width:30px;height:30px;margin:0;padding:0;background:#f1f4f8;color:#42516a;border:1px solid #dce2ea;border-radius:7px;font-size:16px;line-height:1;transition:transform .15s}.collapse-button.collapsed{transform:rotate(-90deg)}.device-body[hidden]{display:none}
.uid{font-family:monospace;background:#eee;padding:6px 10px;border-radius:4px;word-break:break-all;font-size:.85em;margin-bottom:10px}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:.75em;margin-right:4px}.badge-on{background:#d4edda;color:#155724}.badge-off{background:#f8d7da;color:#721c24}.badge-type{background:#e7e7ff;color:#333}
.mode-box{background:#f8f9fa;padding:10px;border-radius:6px;margin-top:10px}.press{border-left:5px solid #dc3545;padding-left:10px}.release{border-left:5px solid #007bff;padding-left:10px}
.usage{display:flex;justify-content:space-between;align-items:center;margin:14px 0;padding:11px 13px;border:1px solid #cddbf8;border-radius:9px;background:#edf3ff;color:#244da7}
.event-tabs{display:flex;gap:4px;padding:4px;background:#edf0f4;border-radius:9px}.event-tab{margin:0;background:transparent;color:#697586}.event-tab.active{background:#fff;color:#18212f;box-shadow:0 1px 4px #bbb}
.event-panel{margin-top:12px}.osc-list{display:grid;gap:10px}.osc-row{display:grid;grid-template-columns:62px minmax(180px,1fr) 115px minmax(100px,.55fr) 68px;gap:9px;align-items:start;padding:12px;border:1px solid #dce2ea;border-radius:10px;background:#fbfcfe}.osc-row label{margin-top:0}.order{display:flex;gap:3px;align-self:center}.mv{width:auto;margin:0;padding:7px;background:#fff;color:#526075;border:1px solid #dce2ea}.remove-msg{width:auto;margin-top:22px;padding:9px;background:#fff3f4;color:#c73c4a;border:1px solid #efc6cb}.add-msg{background:#f7faff;color:#3267e3;border:1px dashed #9db6ef}.add-msg:disabled{background:#eee;color:#888}.empty{display:none;padding:18px;text-align:center;color:#697586;border:1px dashed #dce2ea;border-radius:9px}.osc-list:empty+.empty{display:block}
.sequence-card{margin-top:12px;padding:15px;border:1px solid #dce2ea;border-radius:10px;background:#fbfcfe}.sequence-card h3{margin-top:0}.seq-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.seq-address{grid-column:1/-1}
.encoder-rotation{margin-top:12px;padding:14px;border-left:5px solid #fd7e14;background:#f8f9fa}.encoder-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.encoder-address{grid-column:1/-1}.encoder-mode-hidden{visibility:hidden;pointer-events:none}.angle-section,.tof-section{margin-top:12px;padding:14px;border-left:5px solid #6610f2;background:#f8f9fa}.joystick-section{margin-top:12px;padding:14px;border-left:5px solid #e83e8c;background:#f8f9fa}.angle-grid,.tof-grid,.joystick-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.angle-address,.tof-address,.joystick-address,.joystick-invert{grid-column:1/-1}.angle-section h3,.tof-section h3,.joystick-section h3{margin:0 0 8px}.joystick-invert{display:flex;gap:18px;flex-wrap:wrap}.joystick-invert label{display:flex;align-items:center;gap:6px;margin:0}.joystick-invert input{width:auto;margin:0}.click-section{margin-top:14px;padding:14px;border-left:5px solid #28a745;background:#f8f9fa}.click-section h3,.encoder-rotation h3{margin:0 0 8px}
.save-bar{position:sticky;z-index:15;bottom:8px;display:flex;align-items:center;gap:12px;padding:10px 12px;margin:16px 0 28px;background:rgba(255,255,255,.96);border:1px solid #dce2ea;border-radius:10px;box-shadow:0 5px 18px rgba(0,0,0,.14)}.save-bar button{flex:1;margin:0;background:#28a745}.dirty-status{color:#b45f06;font-weight:bold;white-space:nowrap}.saved-device-card h2{display:flex;align-items:center;gap:4px;flex-wrap:wrap}.btn-warning{background:#ff9800}.toast{position:fixed;z-index:30;left:50%;bottom:78px;transform:translateX(-50%);padding:11px 18px;border-radius:8px;background:#17324d;color:#fff;box-shadow:0 4px 16px rgba(0,0,0,.25)}.wifi-actions{margin-top:28px}.wifi-actions form{margin:0}
.language-row{display:flex;align-items:center;justify-content:space-between;gap:12px}.language-row h2{margin:0}.language-row form{margin:0;min-width:150px}.language-row select{margin:0}
.device-menu-wrap{position:relative}.device-menu-button{width:32px;height:30px;margin:0;padding:0;background:#f1f4f8;color:#42516a;border:1px solid #dce2ea;border-radius:7px;font-size:18px}.device-menu{position:absolute;z-index:20;right:0;top:36px;width:235px;padding:7px;border:1px solid #dce2ea;border-radius:9px;background:#fff;box-shadow:0 8px 24px rgba(0,0,0,.18)}.device-menu[hidden]{display:none}.device-menu a,.device-menu button{display:block;width:100%;margin:0;padding:10px;border:0;border-radius:6px;background:#fff;color:#253047;text-align:left;text-decoration:none;font-size:14px}.device-menu a:hover,.device-menu button:hover{background:#f1f4f8}.tool-row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.tool-row a,.tool-row button{display:block;margin:0;padding:12px;border-radius:6px;background:#3267e3;color:#fff;text-align:center;text-decoration:none;font-size:16px}.import-status{min-height:20px;margin:9px 0 0;color:#526075;font-size:.9em}
@media(max-width:720px){.system-grid,.key-grid,.seq-grid,.encoder-grid,.angle-grid,.tof-grid,.joystick-grid{grid-template-columns:1fr}.seq-address,.encoder-address,.angle-address,.tof-address,.joystick-address,.joystick-invert{grid-column:1}.encoder-mode-hidden{display:none}.osc-row{grid-template-columns:52px 1fr}.osc-row .field,.remove-msg{grid-column:2}}
)CSS";

const char PAGE_SCRIPT[] PROGMEM = R"JS(
const JA=__JA__;const tx=(en,ja)=>JA?ja:en;const MAX_MSG=8;
const enc=new TextEncoder();function bytes(value){return enc.encode(value).length}function limitBytes(input,max){while(bytes(input.value)>max)input.value=input.value.slice(0,-1)}
function validateInput(input){const address=input.classList.contains('msg-address')||input.classList.contains('osc-address'),max=address?192:128,b=bytes(input.value);let error='';if(address){if(!input.value)error=tx('Required','必須です');else if(input.value[0]!=='/')error=tx('Start with /','「/」から始めてください');else if(/[\s#*,?\[\]{}]/.test(input.value))error=tx('Invalid character','使用できない文字があります')}else{const row=input.closest('.osc-row'),type=row?row.querySelector('.type').value:'2';if(type==='0'&&(!input.value.trim()||!Number.isFinite(Number(input.value))))error=tx('Invalid float','Float値が正しくありません');if(type==='1'&&!/^[+-]?\d+$/.test(input.value.trim()))error=tx('Invalid integer','Int値が正しくありません')}if(b>max)error=tx('Too long','長すぎます');input.classList.toggle('invalid',!!error);const small=input.parentNode.querySelector('small');if(small){small.querySelector('.err').textContent=error;small.querySelector('.bytes').textContent=b+' / '+max+' bytes'}return !error}
function limitAndValidate(input,max){limitBytes(input,max);validateInput(input)}function validateSettingsForm(form){let valid=true;form.querySelectorAll('.msg-address,.msg-value,.osc-address').forEach(input=>{if(!validateInput(input))valid=false});if(!valid){const bad=form.querySelector('.invalid');if(bad)bad.focus();alert(tx('Please correct the highlighted OSC fields.','赤く表示されたOSC設定項目を修正してください。'))}return valid}
function markDirty(event){if(event&&event.target&&event.target.matches('input[type="file"]'))return;const status=document.getElementById('dirty-status');if(status)status.hidden=false}
function showToast(message){const toast=document.getElementById('save-toast');toast.textContent=message;toast.hidden=false;clearTimeout(window.toastTimer);window.toastTimer=setTimeout(()=>toast.hidden=true,3000)}
async function saveSettings(event){const form=event.currentTarget;if(!validateSettingsForm(form))return;const button=form.querySelector('.save-bar button');button.disabled=true;try{const response=await fetch('/save-all?ajax=1',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});const message=await response.text();if(!response.ok)throw new Error(message);document.getElementById('dirty-status').hidden=true;showToast(message)}catch(error){alert(error.message||tx('Could not save settings.','設定を保存できませんでした。'))}finally{button.disabled=false}}
async function deleteSavedDevice(event,form){event.preventDefault();if(!confirm(tx('Delete settings for this device?','このデバイスの設定を削除しますか？')))return;try{const response=await fetch('/delete_device?ajax=1',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});const message=await response.text();if(!response.ok)throw new Error(message);form.closest('.saved-device-card').remove();showToast(message)}catch(error){alert(error.message||tx('Could not delete device settings.','デバイス設定を削除できませんでした。'))}}
function toggleDeviceMenu(index){document.querySelectorAll('.device-menu').forEach(menu=>{if(menu.id!=='device-menu-'+index)menu.hidden=true});const menu=document.getElementById('device-menu-'+index);menu.hidden=!menu.hidden}
function chooseSettingsFile(){document.getElementById('settings-import-file').click()}
function showImportError(status,reason){const message=tx('Import failed. The selected JSON file is not valid for this import.','インポートに失敗しました。選択したJSONファイルはこのインポートには使用できません。')+(reason?'\n\n'+reason:'');status.textContent=message.replace(/\n+/g,' ');alert(message)}
async function importSettings(input){const status=document.getElementById('settings-import-status');if(!input.files.length)return;const file=input.files[0];if(file.size>32768){showImportError(status,tx('The JSON file is too large.','JSONファイルが大きすぎます。'));input.value='';return}if(!confirm(tx('Import all settings in this file? Matching device settings will be overwritten.','このファイルの全設定をインポートしますか？同じデバイスの設定は上書きされます。'))){input.value='';return}status.textContent=tx('Importing...','インポート中...');try{const response=await fetch('/import_settings',{method:'POST',headers:{'Content-Type':'application/json'},body:await file.text()});const message=await response.text();if(!response.ok)throw new Error(message);status.textContent=message;setTimeout(()=>location.reload(),800)}catch(error){showImportError(status,error.message)}finally{input.value=''}}
function chooseDevicePreset(index){document.getElementById('preset-file-'+index).click()}
async function identifyDevice(index){document.querySelectorAll('.device-menu').forEach(menu=>menu.hidden=true);try{const response=await fetch('/identify_device?index='+index,{method:'POST'});const message=await response.text();if(!response.ok)throw new Error(message)}catch(error){alert(error.message||tx('The device LED could not be changed.','デバイスのLEDを変更できませんでした。'))}}
async function importDevicePreset(index,input){const status=document.getElementById('preset-status-'+index);if(!input.files.length)return;const file=input.files[0];if(file.size>16384){showImportError(status,tx('E_PRESET_FILE_TOO_LARGE: The preset file exceeds 16 KiB. Select a Device Preset JSON file no larger than 16 KiB.','E_PRESET_FILE_TOO_LARGE: プリセットファイルが16 KiBを超えています。16 KiB以内のDevice Preset JSONファイルを選択してください。'));input.value='';return}if(!confirm(tx('Apply this preset to the selected device? Its settings will be overwritten.','選択したデバイスへこのプリセットを適用しますか？デバイス設定は上書きされます。'))){input.value='';return}status.textContent=tx('Importing preset...','プリセットをインポート中...');try{const response=await fetch('/import_device_preset?index='+index,{method:'POST',headers:{'Content-Type':'application/json'},body:await file.text()});const message=await response.text();if(!response.ok)throw new Error(message);status.textContent=message;setTimeout(()=>location.reload(),700)}catch(error){showImportError(status,error.message)}finally{input.value=''}}
function toggleDevice(index,key){const body=document.getElementById('device-body-'+index);const button=document.getElementById('collapse-'+index);const collapsed=!body.hidden;body.hidden=collapsed;button.classList.toggle('collapsed',collapsed);button.setAttribute('aria-expanded',collapsed?'false':'true');sessionStorage.setItem('chainoscnano-collapse-'+key,collapsed?'1':'0')}
function toggleKeyMode(prId,sqId,select){document.getElementById(prId).style.display=select.value==='1'?'none':'block';document.getElementById(sqId).style.display=select.value==='1'?'block':'none'}
function updateEncoderMode(select){const card=select.closest('.encoder-rotation'),show=select.value==='0';if(card)card.querySelectorAll('.encoder-absolute-setting').forEach(item=>item.classList.toggle('encoder-mode-hidden',!show))}
function showEvent(group,event,button){document.querySelectorAll('.event-panel[data-group="'+group+'"]').forEach(panel=>panel.style.display=panel.dataset.event===event?'block':'none');button.parentNode.querySelectorAll('.event-tab').forEach(tab=>tab.classList.remove('active'));button.classList.add('active')}
function rows(group){return document.querySelectorAll('.osc-row[data-group="'+group+'"]')}
function renumber(group){const all=rows(group);['press','release'].forEach(event=>{document.querySelectorAll('.osc-row[data-group="'+group+'"][data-event="'+event+'"]').forEach((row,index)=>{const prefix=event==='press'?'p':'r';row.querySelector('.msg-address').name=prefix+'_address_'+group+'_'+index;row.querySelector('.type').name=prefix+'_type_'+group+'_'+index;row.querySelector('.msg-value').name=prefix+'_value_'+group+'_'+index})});document.getElementById('count-'+group).textContent=all.length;document.getElementById('p-count-'+group).value=document.querySelectorAll('.osc-row[data-group="'+group+'"][data-event="press"]').length;document.getElementById('r-count-'+group).value=document.querySelectorAll('.osc-row[data-group="'+group+'"][data-event="release"]').length;document.querySelectorAll('.add-msg[data-group="'+group+'"]').forEach(button=>button.disabled=all.length>=MAX_MSG)}
function moveMsg(button,direction){const row=button.closest('.osc-row'),sibling=direction<0?row.previousElementSibling:row.nextElementSibling;if(!sibling)return;direction<0?row.parentNode.insertBefore(row,sibling):row.parentNode.insertBefore(sibling,row);renumber(row.dataset.group);markDirty()}
function removeMsg(button){const row=button.closest('.osc-row'),group=row.dataset.group;row.remove();renumber(group);markDirty()}
function addMsg(button){const group=button.dataset.group,event=button.dataset.event;if(rows(group).length>=MAX_MSG)return;const list=document.getElementById('list-'+event+'-'+group),row=document.createElement('div');row.className='osc-row';row.dataset.group=group;row.dataset.event=event;row.innerHTML='<div class="order"><button type="button" class="mv" onclick="moveMsg(this,-1)">&uarr;</button><button type="button" class="mv" onclick="moveMsg(this,1)">&darr;</button></div><div class="field"><label>'+tx('OSC Address','OSCアドレス')+'</label><input class="msg-address" maxlength="192" required oninput="limitAndValidate(this,192)"><small><span class="err"></span><span class="bytes"></span></small></div><div class="field"><label>'+tx('Type','型')+'</label><select class="type" onchange="validateInput(this.closest(\'.osc-row\').querySelector(\'.msg-value\'))"><option value="0" selected>Float</option><option value="1">Int</option><option value="2">String</option></select><small></small></div><div class="field"><label>'+tx('Value','値')+'</label><input class="msg-value" maxlength="128" value="1.0" oninput="limitAndValidate(this,128)"><small><span class="err"></span><span class="bytes"></span></small></div><button type="button" class="remove-msg" onclick="removeMsg(this)">'+tx('Delete','削除')+'</button>';list.appendChild(row);renumber(group);markDirty();row.querySelector('.msg-address').focus()}
document.addEventListener('click',event=>{if(!event.target.closest('.device-menu-wrap'))document.querySelectorAll('.device-menu').forEach(menu=>menu.hidden=true)});document.addEventListener('DOMContentLoaded',()=>{const groups=new Set;document.querySelectorAll('.add-msg[data-group]').forEach(button=>groups.add(button.dataset.group));groups.forEach(renumber);document.querySelectorAll('.msg-address,.msg-value,.osc-address').forEach(validateInput);const form=document.getElementById('settings-form');if(form){form.addEventListener('input',markDirty);form.addEventListener('change',markDirty)}document.querySelectorAll('.device[data-collapse-key]').forEach(card=>{const key=card.dataset.collapseKey,index=card.dataset.deviceIndex;if(sessionStorage.getItem('chainoscnano-collapse-'+key)==='1'){const body=document.getElementById('device-body-'+index),button=document.getElementById('collapse-'+index);body.hidden=true;button.classList.add('collapsed');button.setAttribute('aria-expanded','false')}})})
)JS";

String pageStart(const char* title) {
  String html;
  html.reserve(2200);
  html += F("<!doctype html><html lang='");
  html += isJapaneseUi() ? F("ja") : F("en");
  html += F("'><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>");
  html += title;
  html += F("</title><style>");
  html += FPSTR(PAGE_STYLE);
  html += F("</style><script>");
  html += FPSTR(PAGE_SCRIPT);
  html += F("</script></head><body><main>");
  html.replace("__JA__", isJapaneseUi() ? "true" : "false");
  return html;
}

void sendPage(String html) {
  html += F("</main></body></html>");
  server.send(200, "text/html; charset=utf-8", html);
}

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t index = 0; index < value.length(); ++index) {
    switch (value[index]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += value[index]; break;
    }
  }
  return escaped;
}

String jsonString(const String& value) {
  String output = "\"";
  for (size_t index = 0; index < value.length(); ++index) {
    const uint8_t c = static_cast<uint8_t>(value[index]);
    if (c == '"') output += "\\\"";
    else if (c == '\\') output += "\\\\";
    else if (c == '\b') output += "\\b";
    else if (c == '\f') output += "\\f";
    else if (c == '\n') output += "\\n";
    else if (c == '\r') output += "\\r";
    else if (c == '\t') output += "\\t";
    else if (c < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04X", c);
      output += escaped;
    } else output += static_cast<char>(c);
  }
  output += '"';
  return output;
}

String messageJson(const KeyOscMessage& message) {
  return String("{\"address\":") + jsonString(message.address) +
         ",\"value\":" + jsonString(message.valueStr) +
         ",\"type\":" + String(static_cast<int>(message.valueType)) + "}";
}

String messageArrayJson(const KeyOscMessage* messages, uint8_t count) {
  String output = "[";
  for (uint8_t index = 0; index < count; ++index) {
    if (index) output += ',';
    output += messageJson(messages[index]);
  }
  output += ']';
  return output;
}

String sequenceJson(const KeySequenceConfig& sequence) {
  return String("{\"address\":") + jsonString(sequence.address) +
         ",\"type\":" + String(static_cast<int>(sequence.valueType)) +
         ",\"start\":" + String(sequence.start, 6) +
         ",\"end\":" + String(sequence.end, 6) +
         ",\"step\":" + String(sequence.step, 6) + "}";
}

String keySettingJson(const KeySetting& setting, bool includeIdentity) {
  String output = "{";
  if (includeIdentity) {
    output += String("\"identity\":") + jsonString(setting.identity) +
              ",\"deviceType\":" + String(CHAIN_KEY_DEVICE_TYPE) +
              ",\"deviceTypeName\":\"Key\"" +
              ",\"displayName\":" + jsonString(setting.displayName) +
              ",\"builtIn\":" + String(setting.builtIn ? "true" : "false");
  } else {
    output += String("\"format\":") + jsonString(DEVICE_PRESET_FORMAT_NAME) +
              ",\"schemaVersion\":" + String(DEVICE_PRESET_SCHEMA_VERSION) +
              ",\"deviceType\":" + String(CHAIN_KEY_DEVICE_TYPE) +
              ",\"deviceTypeName\":\"Key\"";
  }
  output += String(",\"key\":{\"mode\":") + String(static_cast<int>(setting.mode)) +
            ",\"press\":" + messageArrayJson(setting.pressMessages, setting.pressMessageCount) +
            ",\"release\":" + messageArrayJson(setting.releaseMessages, setting.releaseMessageCount) +
            ",\"sequence\":" + sequenceJson(setting.sequence) + "}}";
  return output;
}

String encoderSettingJson(const EncoderSetting& setting, bool includeIdentity) {
  String output = "{";
  if (includeIdentity) {
    output += String("\"identity\":") + jsonString(setting.identity) +
              ",\"deviceType\":" + String(CHAIN_ENCODER_DEVICE_TYPE) +
              ",\"deviceTypeName\":\"Encoder\"" +
              ",\"displayName\":" + jsonString(setting.displayName) +
              ",\"builtIn\":false";
  } else {
    output += String("\"format\":") + jsonString(DEVICE_PRESET_FORMAT_NAME) +
              ",\"schemaVersion\":" + String(DEVICE_PRESET_SCHEMA_VERSION) +
              ",\"deviceType\":" + String(CHAIN_ENCODER_DEVICE_TYPE) +
              ",\"deviceTypeName\":\"Encoder\"";
  }
  output += String(",\"encoder\":{\"rotationAddress\":") +
            jsonString(setting.rotationAddress) +
            ",\"sendIncrement\":" +
            String(setting.sendIncrement ? "true" : "false") +
            ",\"absoluteInputMin\":" + String(setting.absoluteInputMin, 6) +
            ",\"absoluteInputMax\":" + String(setting.absoluteInputMax, 6) +
            ",\"incrementScale\":" + String(setting.incrementScale, 6) +
            ",\"range\":{\"outMin\":" + String(setting.outputMin, 6) +
            ",\"outMax\":" + String(setting.outputMax, 6) +
            ",\"type\":" + String(static_cast<int>(setting.outputType)) + "}" +
            ",\"clickMode\":" + String(static_cast<int>(setting.clickMode)) +
            ",\"press\":" +
            messageArrayJson(setting.pressMessages, setting.pressMessageCount) +
            ",\"release\":" +
            messageArrayJson(setting.releaseMessages, setting.releaseMessageCount) +
            ",\"sequence\":" + sequenceJson(setting.clickSequence) + "}}";
  return output;
}

String angleSettingJson(const AngleSetting& setting, bool includeIdentity) {
  String output = "{";
  if (includeIdentity) {
    output += String("\"identity\":") + jsonString(setting.identity) +
              ",\"deviceType\":" + String(CHAIN_ANGLE_DEVICE_TYPE) +
              ",\"deviceTypeName\":\"Angle\"" +
              ",\"displayName\":" + jsonString(setting.displayName) +
              ",\"builtIn\":false";
  } else {
    output += String("\"format\":") + jsonString(DEVICE_PRESET_FORMAT_NAME) +
              ",\"schemaVersion\":" + String(DEVICE_PRESET_SCHEMA_VERSION) +
              ",\"deviceType\":" + String(CHAIN_ANGLE_DEVICE_TYPE) +
              ",\"deviceTypeName\":\"Angle\"";
  }
  output += String(",\"angle\":{\"address\":") + jsonString(setting.address) +
            ",\"use12bit\":" + String(setting.use12Bit ? "true" : "false") +
            ",\"deadband\":" + String(setting.deadband) +
            ",\"range\":{\"outMin\":" + String(setting.outputMin, 6) +
            ",\"outMax\":" + String(setting.outputMax, 6) +
            ",\"type\":" + String(static_cast<int>(setting.outputType)) + "}}}";
  return output;
}

String tofSettingJson(const TofSetting& setting, bool includeIdentity) {
  String output = "{";
  if (includeIdentity) output += String("\"identity\":") + jsonString(setting.identity) +
      ",\"deviceType\":" + String(CHAIN_TOF_DEVICE_TYPE) + ",\"deviceTypeName\":\"ToF\",\"displayName\":" + jsonString(setting.displayName) + ",\"builtIn\":false";
  else output += String("\"format\":") + jsonString(DEVICE_PRESET_FORMAT_NAME) +
      ",\"schemaVersion\":" + String(DEVICE_PRESET_SCHEMA_VERSION) + ",\"deviceType\":" + String(CHAIN_TOF_DEVICE_TYPE) + ",\"deviceTypeName\":\"ToF\"";
  output += String(",\"tof\":{\"address\":") + jsonString(setting.address) +
      ",\"deadband\":" + String(setting.deadband) + ",\"maxDistanceMm\":" + String(setting.maxDistanceMm) +
      ",\"nearValueHigh\":" + String(setting.nearValueHigh ? "true" : "false") +
      ",\"range\":{\"outMin\":" + String(setting.outputMin, 6) + ",\"outMax\":" + String(setting.outputMax, 6) +
      ",\"type\":" + String(static_cast<int>(setting.outputType)) + "}}}";
  return output;
}

String joystickSettingJson(const JoystickSetting& setting, bool includeIdentity) {
  String output = "{";
  if (includeIdentity) output += String("\"identity\":") + jsonString(setting.identity) +
      ",\"deviceType\":" + String(CHAIN_JOYSTICK_DEVICE_TYPE) + ",\"deviceTypeName\":\"Joystick\",\"displayName\":" + jsonString(setting.displayName) + ",\"builtIn\":false";
  else output += String("\"format\":") + jsonString(DEVICE_PRESET_FORMAT_NAME) +
      ",\"schemaVersion\":" + String(DEVICE_PRESET_SCHEMA_VERSION) + ",\"deviceType\":" + String(CHAIN_JOYSTICK_DEVICE_TYPE) + ",\"deviceTypeName\":\"Joystick\"";
  output += String(",\"joystick\":{\"xAddress\":") + jsonString(setting.xAddress) +
      ",\"yAddress\":" + jsonString(setting.yAddress) + ",\"deadband\":" + String(setting.deadband) +
      ",\"invertX\":" + String(setting.invertX ? "true" : "false") + ",\"invertY\":" + String(setting.invertY ? "true" : "false") +
      ",\"range\":{\"outMin\":" + String(setting.outputMin,6) + ",\"outMax\":" + String(setting.outputMax,6) + ",\"type\":" + String((int)setting.outputType) + "}" +
      ",\"clickMode\":" + String((int)setting.clickMode) + ",\"press\":" + messageArrayJson(setting.pressMessages,setting.pressMessageCount) +
      ",\"release\":" + messageArrayJson(setting.releaseMessages,setting.releaseMessageCount) + ",\"sequence\":" + sequenceJson(setting.clickSequence) + "}}";
  return output;
}

bool presetError(String& error, const char* english, const char* japanese) {
  error = tr(english, japanese);
  return false;
}

bool presetRequiredFieldMissing(String& error) {
  return presetError(error,
      "E_PRESET_REQUIRED_FIELD_MISSING: A required preset field is missing. Use a file that contains all fields required by Device Preset v1.",
      "E_PRESET_REQUIRED_FIELD_MISSING: プリセットに必須項目がありません。Device Preset v1の必須項目を含むファイルを使用してください。");
}

bool presetFieldTypeInvalid(String& error) {
  return presetError(error,
      "E_PRESET_FIELD_TYPE_INVALID: A preset field has an invalid JSON type. Use the JSON type defined by Device Preset v1.",
      "E_PRESET_FIELD_TYPE_INVALID: プリセット項目の型が正しくありません。Device Preset v1で定義されたJSON型を使用してください。");
}

bool oscTypeInvalid(String& error) {
  return presetError(error,
      "E_OSC_TYPE_INVALID: OSC Type is invalid. Select Float, Int, or String as allowed for this field.",
      "E_OSC_TYPE_INVALID: OSC Typeが正しくありません。この項目で使用できるFloat、Int、Stringのいずれかを指定してください。");
}

bool presetDeviceSettingInvalid(String& error) {
  return presetError(error,
      "E_PRESET_DEVICE_SETTING_INVALID: A device setting is invalid. Check the allowed value range and type for the target device.",
      "E_PRESET_DEVICE_SETTING_INVALID: デバイス設定値が正しくありません。対象デバイスで使用できる値の範囲と型を確認してください。");
}

bool hasPresetFields(JsonObjectConst object, const char* const* fields,
                     size_t count, String& error) {
  for (size_t index = 0; index < count; ++index)
    if (!object.containsKey(fields[index])) return presetRequiredFieldMissing(error);
  return true;
}

bool validJsonAddress(String address, String& error) {
  address.trim();
  if (address.length() > 192)
    return presetError(error,
        "E_OSC_ADDRESS_TOO_LONG: OSC Address is too long. Keep it within 192 bytes in UTF-8.",
        "E_OSC_ADDRESS_TOO_LONG: OSC Addressが長すぎます。UTF-8で192バイト以内にしてください。");
  if (address.isEmpty() || address[0] != '/')
    return presetError(error,
        "E_OSC_ADDRESS_INVALID: OSC Address must start with `/` and must not contain whitespace or `# * , ? [ ] { }`.",
        "E_OSC_ADDRESS_INVALID: OSC Addressは「/」から始め、空白および`# * , ? [ ] { }`を含めないでください。");
  for (size_t index = 0; index < address.length(); ++index) {
    const char c = address[index];
    if (isspace(static_cast<unsigned char>(c)) || c == '#' || c == '*' ||
        c == ',' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}') {
      return presetError(error,
          "E_OSC_ADDRESS_INVALID: OSC Address must start with `/` and must not contain whitespace or `# * , ? [ ] { }`.",
          "E_OSC_ADDRESS_INVALID: OSC Addressは「/」から始め、空白および`# * , ? [ ] { }`を含めないでください。");
    }
  }
  return true;
}

bool validatePresetMessage(JsonObjectConst object, String& error) {
  if (object.isNull()) return presetFieldTypeInvalid(error);
  static const char* const required[] = {"address", "value", "type"};
  if (!hasPresetFields(object, required, 3, error)) return false;
  if (!object["address"].is<const char*>() || !object["value"].is<const char*>() ||
      !object["type"].is<int>()) return presetFieldTypeInvalid(error);
  String address = object["address"].as<const char*>();
  if (!validJsonAddress(address, error)) return false;
  String value = object["value"].as<const char*>();
  if (value.length() > 128)
    return presetError(error,
        "E_OSC_VALUE_TOO_LONG: OSC Value is too long. Keep it within 128 bytes in UTF-8.",
        "E_OSC_VALUE_TOO_LONG: OSC Valueが長すぎます。UTF-8で128バイト以内にしてください。");
  const int type = object["type"].as<int>();
  if (type < TYPE_FLOAT || type > TYPE_STRING) return oscTypeInvalid(error);
  if (type == TYPE_FLOAT) {
    char* end = nullptr;
    const float parsed = strtof(value.c_str(), &end);
    if (!end || end == value.c_str() || *end != '\0' || !isfinite(parsed))
      return presetError(error,
          "E_OSC_FLOAT32_INVALID: The Float value is invalid. Specify a decimal number representable as a finite OSC float32.",
          "E_OSC_FLOAT32_INVALID: Float値が正しくありません。有限のOSC float32として表現できる10進数を指定してください。");
  } else if (type == TYPE_INT) {
    errno = 0;
    char* end = nullptr;
    const long parsed = strtol(value.c_str(), &end, 10);
    if (!end || end == value.c_str() || *end != '\0' || errno == ERANGE ||
        parsed < INT32_MIN || parsed > INT32_MAX)
      return presetError(error,
          "E_OSC_INT32_INVALID: The Int value is invalid. Specify a decimal integer from `-2147483648` to `2147483647`.",
          "E_OSC_INT32_INVALID: Int値が正しくありません。`-2147483648`～`2147483647`の範囲の10進整数を指定してください。");
  }
  return true;
}

bool validatePresetMessages(JsonVariantConst pressValue,
                            JsonVariantConst releaseValue, String& error) {
  if (!pressValue.is<JsonArrayConst>() || !releaseValue.is<JsonArrayConst>())
    return presetFieldTypeInvalid(error);
  JsonArrayConst press = pressValue.as<JsonArrayConst>();
  JsonArrayConst release = releaseValue.as<JsonArrayConst>();
  if (press.size() + release.size() > MAX_KEY_OSC_MESSAGES)
    return presetError(error,
        "E_OSC_MESSAGE_COUNT_EXCEEDED: Press and Release OSC messages must total 8 or fewer.",
        "E_OSC_MESSAGE_COUNT_EXCEEDED: PressとReleaseのOSCメッセージは、合計8件以内にしてください。");
  for (JsonVariantConst item : press) {
    if (!item.is<JsonObjectConst>()) return presetFieldTypeInvalid(error);
    if (!validatePresetMessage(item.as<JsonObjectConst>(), error)) return false;
  }
  for (JsonVariantConst item : release) {
    if (!item.is<JsonObjectConst>()) return presetFieldTypeInvalid(error);
    if (!validatePresetMessage(item.as<JsonObjectConst>(), error)) return false;
  }
  return true;
}

bool validatePresetSequence(JsonObjectConst object, bool legacy,
                            String& error) {
  if (object.isNull()) return presetFieldTypeInvalid(error);
  static const char* const required[] = {"address", "type", "start", "end", "step"};
  if (!hasPresetFields(object, required, 5, error))
    return presetError(error,
        "E_SEQUENCE_REQUIRED_FIELD_MISSING: A required Sequence field is missing. Specify Address, Type, Start, End, and Step.",
        "E_SEQUENCE_REQUIRED_FIELD_MISSING: Sequenceに必須項目がありません。Address、Type、Start、End、Stepをすべて指定してください。");
  if (!object["address"].is<const char*>() || !object["type"].is<int>() ||
      !object["start"].is<float>() || !object["end"].is<float>() ||
      !object["step"].is<float>()) return presetFieldTypeInvalid(error);
  String address = object["address"].as<const char*>();
  if (!validJsonAddress(address, error)) return false;
  const int type = object["type"].as<int>();
  if ((type < TYPE_FLOAT || type > TYPE_STRING) && !legacy)
    return oscTypeInvalid(error);
  const float start = object["start"].as<float>();
  const float end = object["end"].as<float>();
  const float step = object["step"].as<float>();
  if (!isfinite(start) || !isfinite(end) || !isfinite(step))
    return presetError(error,
        "E_SEQUENCE_VALUE_INVALID: A Sequence number is invalid. Specify finite numbers for Start, End, and Step.",
        "E_SEQUENCE_VALUE_INVALID: Sequenceの数値が正しくありません。Start、End、Stepには有限の数値を指定してください。");
  if (step == 0.0f)
    return presetError(error,
        "E_SEQUENCE_STEP_ZERO: Sequence Step must not be zero. Specify a non-zero value that moves from Start toward End.",
        "E_SEQUENCE_STEP_ZERO: SequenceのStepには0を指定できません。StartからEndへ進む0以外の値を指定してください。");
  if ((start < end && step < 0.0f) || (start > end && step > 0.0f))
    return presetError(error,
        "E_SEQUENCE_DIRECTION_INVALID: Sequence direction is invalid. Use a positive Step when Start is below End and a negative Step when Start is above End.",
        "E_SEQUENCE_DIRECTION_INVALID: Sequenceの進行方向が正しくありません。StartがEndより小さい場合は正のStep、大きい場合は負のStepを指定してください。");
  return true;
}

bool validatePresetRange(JsonObjectConst range, bool numericOnly, bool legacy,
                         String& error) {
  if (range.isNull()) return presetFieldTypeInvalid(error);
  static const char* const required[] = {"outMin", "outMax", "type"};
  if (!hasPresetFields(range, required, 3, error)) return false;
  if (!range["outMin"].is<float>() || !range["outMax"].is<float>() ||
      !range["type"].is<int>()) return presetFieldTypeInvalid(error);
  const int type = range["type"].as<int>();
  if (type < TYPE_FLOAT || type > TYPE_STRING) return oscTypeInvalid(error);
  if (numericOnly && type == TYPE_STRING && !legacy) return oscTypeInvalid(error);
  if (!isfinite(range["outMin"].as<float>()) ||
      !isfinite(range["outMax"].as<float>()))
    return presetDeviceSettingInvalid(error);
  return true;
}

bool validateDevicePreset(JsonObjectConst root, int deviceType, bool legacy,
                          String& error) {
  if (!root.containsKey("deviceTypeName")) return presetRequiredFieldMissing(error);
  if (!root["deviceTypeName"].is<const char*>()) return presetFieldTypeInvalid(error);

  if (deviceType == CHAIN_KEY_DEVICE_TYPE) {
    if (!root.containsKey("key")) return presetRequiredFieldMissing(error);
    JsonObjectConst key = root["key"].as<JsonObjectConst>();
    if (key.isNull()) return presetFieldTypeInvalid(error);
    static const char* const required[] = {"mode", "press", "release", "sequence"};
    if (!hasPresetFields(key, required, 4, error)) return false;
    if (!key["mode"].is<int>() || !key["press"].is<JsonArrayConst>() ||
        !key["release"].is<JsonArrayConst>() ||
        !key["sequence"].is<JsonObjectConst>()) return presetFieldTypeInvalid(error);
    const int mode = key["mode"].as<int>();
    if (mode < MODE_PRESS_RELEASE || mode > MODE_SEQUENCE)
      return presetDeviceSettingInvalid(error);
    return validatePresetMessages(key["press"], key["release"], error) &&
           validatePresetSequence(key["sequence"].as<JsonObjectConst>(), legacy, error);
  }

  if (deviceType == CHAIN_ENCODER_DEVICE_TYPE) {
    if (!root.containsKey("encoder")) return presetRequiredFieldMissing(error);
    JsonObjectConst encoder = root["encoder"].as<JsonObjectConst>();
    if (encoder.isNull()) return presetFieldTypeInvalid(error);
    static const char* const required[] = {
        "rotationAddress", "sendIncrement", "absoluteInputMin",
        "absoluteInputMax", "incrementScale", "range", "clickMode",
        "press", "release", "sequence"};
    if (!hasPresetFields(encoder, required, 10, error)) return false;
    if (!encoder["rotationAddress"].is<const char*>() ||
        !encoder["sendIncrement"].is<bool>() ||
        !encoder["absoluteInputMin"].is<float>() ||
        !encoder["absoluteInputMax"].is<float>() ||
        !encoder["incrementScale"].is<float>() ||
        !encoder["range"].is<JsonObjectConst>() ||
        !encoder["clickMode"].is<int>() ||
        !encoder["press"].is<JsonArrayConst>() ||
        !encoder["release"].is<JsonArrayConst>() ||
        !encoder["sequence"].is<JsonObjectConst>()) return presetFieldTypeInvalid(error);
    String address = encoder["rotationAddress"].as<const char*>();
    if (!validJsonAddress(address, error)) return false;
    if (!isfinite(encoder["absoluteInputMin"].as<float>()) ||
        !isfinite(encoder["absoluteInputMax"].as<float>()) ||
        !isfinite(encoder["incrementScale"].as<float>()))
      return presetDeviceSettingInvalid(error);
    const int mode = encoder["clickMode"].as<int>();
    if (mode < MODE_PRESS_RELEASE || mode > MODE_SEQUENCE)
      return presetDeviceSettingInvalid(error);
    return validatePresetRange(encoder["range"].as<JsonObjectConst>(), false,
                               legacy, error) &&
           validatePresetMessages(encoder["press"], encoder["release"], error) &&
           validatePresetSequence(encoder["sequence"].as<JsonObjectConst>(), legacy,
                                  error);
  }

  if (deviceType == CHAIN_ANGLE_DEVICE_TYPE) {
    if (!root.containsKey("angle")) return presetRequiredFieldMissing(error);
    JsonObjectConst angle = root["angle"].as<JsonObjectConst>();
    if (angle.isNull()) return presetFieldTypeInvalid(error);
    static const char* const required[] = {"address", "use12bit", "deadband", "range"};
    if (!hasPresetFields(angle, required, 4, error)) return false;
    if (!angle["address"].is<const char*>() || !angle["use12bit"].is<bool>() ||
        !angle["deadband"].is<int>() || !angle["range"].is<JsonObjectConst>())
      return presetFieldTypeInvalid(error);
    String address = angle["address"].as<const char*>();
    if (!validJsonAddress(address, error)) return false;
    if (angle["deadband"].as<int>() < 1) return presetDeviceSettingInvalid(error);
    return validatePresetRange(angle["range"].as<JsonObjectConst>(), false,
                               legacy, error);
  }

  if (deviceType == CHAIN_JOYSTICK_DEVICE_TYPE) {
    if (!root.containsKey("joystick")) return presetRequiredFieldMissing(error);
    JsonObjectConst joystick = root["joystick"].as<JsonObjectConst>();
    if (joystick.isNull()) return presetFieldTypeInvalid(error);
    static const char* const required[] = {
        "xAddress", "yAddress", "deadband", "invertX", "invertY", "range",
        "clickMode", "press", "release", "sequence"};
    if (!hasPresetFields(joystick, required, 10, error)) return false;
    if (!joystick["xAddress"].is<const char*>() ||
        !joystick["yAddress"].is<const char*>() ||
        !joystick["deadband"].is<int>() || !joystick["invertX"].is<bool>() ||
        !joystick["invertY"].is<bool>() ||
        !joystick["range"].is<JsonObjectConst>() ||
        !joystick["clickMode"].is<int>() ||
        !joystick["press"].is<JsonArrayConst>() ||
        !joystick["release"].is<JsonArrayConst>() ||
        !joystick["sequence"].is<JsonObjectConst>()) return presetFieldTypeInvalid(error);
    String xAddress = joystick["xAddress"].as<const char*>();
    String yAddress = joystick["yAddress"].as<const char*>();
    if (!validJsonAddress(xAddress, error) || !validJsonAddress(yAddress, error))
      return false;
    const int deadband = joystick["deadband"].as<int>();
    const int mode = joystick["clickMode"].as<int>();
    if (deadband < 1 || deadband > 254 || mode < MODE_PRESS_RELEASE ||
        mode > MODE_SEQUENCE) return presetDeviceSettingInvalid(error);
    return validatePresetRange(joystick["range"].as<JsonObjectConst>(), false,
                               legacy, error) &&
           validatePresetMessages(joystick["press"], joystick["release"], error) &&
           validatePresetSequence(joystick["sequence"].as<JsonObjectConst>(), legacy,
                                  error);
  }

  if (deviceType == CHAIN_TOF_DEVICE_TYPE) {
    if (!root.containsKey("tof")) return presetRequiredFieldMissing(error);
    JsonObjectConst tof = root["tof"].as<JsonObjectConst>();
    if (tof.isNull()) return presetFieldTypeInvalid(error);
    static const char* const required[] = {
        "address", "deadband", "maxDistanceMm", "nearValueHigh", "range"};
    if (!hasPresetFields(tof, required, 5, error)) return false;
    if (!tof["address"].is<const char*>() || !tof["deadband"].is<int>() ||
        !tof["maxDistanceMm"].is<int>() || !tof["nearValueHigh"].is<bool>() ||
        !tof["range"].is<JsonObjectConst>()) return presetFieldTypeInvalid(error);
    String address = tof["address"].as<const char*>();
    if (!validJsonAddress(address, error)) return false;
    const int deadband = tof["deadband"].as<int>();
    const int maxDistance = tof["maxDistanceMm"].as<int>();
    if (deadband < 1 || deadband > 2000 || maxDistance < 31 || maxDistance > 2000)
      return presetDeviceSettingInvalid(error);
    return validatePresetRange(tof["range"].as<JsonObjectConst>(), true, legacy,
                               error);
  }
  return false;
}

void normalizeLegacyPresetTypes(JsonObject root, int deviceType) {
  if (deviceType == CHAIN_KEY_DEVICE_TYPE) {
    JsonObject sequence = root["key"]["sequence"].as<JsonObject>();
    const int type = sequence["type"] | TYPE_FLOAT;
    if (type < TYPE_FLOAT || type > TYPE_STRING) sequence["type"] = TYPE_FLOAT;
  } else if (deviceType == CHAIN_ENCODER_DEVICE_TYPE) {
    JsonObject sequence = root["encoder"]["sequence"].as<JsonObject>();
    const int type = sequence["type"] | TYPE_FLOAT;
    if (type < TYPE_FLOAT || type > TYPE_STRING) sequence["type"] = TYPE_FLOAT;
  } else if (deviceType == CHAIN_JOYSTICK_DEVICE_TYPE) {
    JsonObject sequence = root["joystick"]["sequence"].as<JsonObject>();
    const int type = sequence["type"] | TYPE_FLOAT;
    if (type < TYPE_FLOAT || type > TYPE_STRING) sequence["type"] = TYPE_FLOAT;
  } else if (deviceType == CHAIN_TOF_DEVICE_TYPE) {
    JsonObject range = root["tof"]["range"].as<JsonObject>();
    if ((range["type"] | TYPE_FLOAT) == TYPE_STRING) range["type"] = TYPE_FLOAT;
  }
}

bool jsonMessage(JsonObjectConst object, KeyOscMessage& message, String& error) {
  if (object.isNull() || !object["address"].is<const char*>() ||
      !object["value"].is<const char*>() || !object["type"].is<int>()) {
    error = tr("OSC message fields are missing.", "OSCメッセージの必須項目がありません。");
    return false;
  }
  message.address = object["address"].as<const char*>();
  message.valueStr = object["value"].as<const char*>();
  const int type = object["type"].as<int>();
  if (!validJsonAddress(message.address, error)) return false;
  if (type < TYPE_FLOAT || type > TYPE_STRING) {
    error = tr("OSC message type is invalid.", "OSCメッセージの型が正しくありません。");
    return false;
  }
  if (message.valueStr.length() > 128) {
    error = tr("E_OSC_VALUE_TOO_LONG: OSC Value is too long. Keep it within 128 bytes in UTF-8.",
               "E_OSC_VALUE_TOO_LONG: OSC Valueが長すぎます。UTF-8で128バイト以内にしてください。");
    return false;
  }
  message.valueType = static_cast<ValueType>(type);
  if (message.valueType == TYPE_INT) {
    char* end = nullptr;
    errno = 0;
    const long value = strtol(message.valueStr.c_str(), &end, 10);
    if (!end || end == message.valueStr.c_str() || *end != '\0' || errno == ERANGE ||
        value < INT32_MIN || value > INT32_MAX) {
      error = tr("Integer value is invalid.", "Int値が正しくありません。");
      return false;
    }
  } else if (message.valueType == TYPE_FLOAT) {
    char* end = nullptr;
    const float value = strtof(message.valueStr.c_str(), &end);
    if (!end || end == message.valueStr.c_str() || *end != '\0' || !isfinite(value)) {
      error = tr("Float value is invalid.", "Float値が正しくありません。");
      return false;
    }
  }
  return true;
}

bool jsonSequence(JsonObjectConst object, KeySequenceConfig& sequence, String& error) {
  if (object.isNull() || !object["address"].is<const char*>() ||
      !object["type"].is<int>() || !object.containsKey("start") ||
      !object.containsKey("end") || !object.containsKey("step")) {
    error = tr("Sequence fields are missing.", "シーケンスの必須項目がありません。");
    return false;
  }
  sequence.address = object["address"].as<const char*>();
  const int type = object["type"].as<int>();
  sequence.start = object["start"].as<float>();
  sequence.end = object["end"].as<float>();
  sequence.step = object["step"].as<float>();
  if (type < TYPE_FLOAT || type > TYPE_STRING ||
      !isfinite(sequence.start) || !isfinite(sequence.end) ||
      !isfinite(sequence.step) || !validJsonAddress(sequence.address, error)) {
    if (error.isEmpty()) error = tr("Sequence values are invalid.", "シーケンスの値が正しくありません。");
    return false;
  }
  if (sequence.step == 0.0f) {
    error = tr("E_SEQUENCE_STEP_ZERO: Sequence Step must not be zero. Specify a non-zero value that moves from Start toward End.",
               "E_SEQUENCE_STEP_ZERO: SequenceのStepには0を指定できません。StartからEndへ進む0以外の値を指定してください。");
    return false;
  }
  if ((sequence.start < sequence.end && sequence.step < 0.0f) ||
      (sequence.start > sequence.end && sequence.step > 0.0f)) {
    error = tr("E_SEQUENCE_DIRECTION_INVALID: Sequence direction is invalid. Use a positive Step when Start is below End and a negative Step when Start is above End.",
               "E_SEQUENCE_DIRECTION_INVALID: Sequenceの進行方向が正しくありません。StartがEndより小さい場合は正のStep、大きい場合は負のStepを指定してください。");
    return false;
  }
  sequence.valueType = static_cast<ValueType>(type);
  keySettingsNormalizeSequence(sequence);
  return true;
}

bool keySettingFromJson(JsonObjectConst object, KeySetting& candidate,
                        bool includeIdentity, String& error) {
  if (object.isNull() || !object["deviceType"].is<int>() ||
      object["deviceType"].as<int>() != CHAIN_KEY_DEVICE_TYPE) {
    error = tr("Device type is not Key.", "デバイス種類がKeyではありません。");
    return false;
  }
  if (includeIdentity) {
    if (!object["identity"].is<const char*>() ||
        !object["displayName"].is<const char*>()) {
      error = tr("Device identity or name is missing.", "デバイス識別子または名前がありません。");
      return false;
    }
    candidate.identity = object["identity"].as<const char*>();
    candidate.displayName = object["displayName"].as<const char*>();
    candidate.identity.trim();
    candidate.displayName.trim();
    const bool validIdentity = candidate.identity == "nano:button" ||
                               (candidate.identity.startsWith("chain:") &&
                                candidate.identity.length() > 6);
    if (!validIdentity || candidate.displayName.isEmpty() || candidate.displayName.length() > 64) {
      error = tr("Device identity or name is invalid.", "デバイス識別子または名前が正しくありません。");
      return false;
    }
    candidate.builtIn = candidate.identity == "nano:button";
  }
  JsonObjectConst key = object["key"].as<JsonObjectConst>();
  if (key.isNull() || !key["mode"].is<int>() ||
      !key["press"].is<JsonArrayConst>() || !key["release"].is<JsonArrayConst>()) {
    error = tr("Key settings are missing.", "Key設定がありません。");
    return false;
  }
  const int mode = key["mode"].as<int>();
  JsonArrayConst press = key["press"].as<JsonArrayConst>();
  JsonArrayConst release = key["release"].as<JsonArrayConst>();
  if (mode < MODE_PRESS_RELEASE || mode > MODE_SEQUENCE ||
      press.size() + release.size() > MAX_KEY_OSC_MESSAGES) {
    error = tr("Key mode or message count is invalid.", "キーモードまたはメッセージ数が正しくありません。");
    return false;
  }
  candidate.mode = static_cast<KeyMode>(mode);
  candidate.pressMessageCount = static_cast<uint8_t>(press.size());
  candidate.releaseMessageCount = static_cast<uint8_t>(release.size());
  uint8_t index = 0;
  for (JsonObjectConst message : press)
    if (!jsonMessage(message, candidate.pressMessages[index++], error)) return false;
  index = 0;
  for (JsonObjectConst message : release)
    if (!jsonMessage(message, candidate.releaseMessages[index++], error)) return false;
  return jsonSequence(key["sequence"].as<JsonObjectConst>(), candidate.sequence, error);
}

bool encoderSettingFromJson(JsonObjectConst object, EncoderSetting& candidate,
                            bool includeIdentity, String& error) {
  if (object.isNull() || !object["deviceType"].is<int>() ||
      object["deviceType"].as<int>() != CHAIN_ENCODER_DEVICE_TYPE) {
    error = tr("Device type is not Encoder.", "デバイス種類がEncoderではありません。");
    return false;
  }
  if (includeIdentity) {
    if (!object["identity"].is<const char*>() ||
        !object["displayName"].is<const char*>()) {
      error = tr("Device identity or name is missing.", "デバイス識別子または名前がありません。");
      return false;
    }
    candidate.identity = object["identity"].as<const char*>();
    candidate.displayName = object["displayName"].as<const char*>();
    candidate.identity.trim();
    candidate.displayName.trim();
    if (!candidate.identity.startsWith("chain:") ||
        candidate.identity.length() <= 6 || candidate.displayName.isEmpty() ||
        candidate.displayName.length() > 64) {
      error = tr("Device identity or name is invalid.", "デバイス識別子または名前が正しくありません。");
      return false;
    }
  }
  JsonObjectConst encoder = object["encoder"].as<JsonObjectConst>();
  JsonObjectConst range = encoder["range"].as<JsonObjectConst>();
  if (encoder.isNull() || range.isNull() ||
      !encoder["rotationAddress"].is<const char*>() ||
      !encoder.containsKey("absoluteInputMin") ||
      !encoder.containsKey("absoluteInputMax") ||
      !encoder.containsKey("incrementScale") ||
      !range.containsKey("outMin") || !range.containsKey("outMax") ||
      !range["type"].is<int>() || !encoder["press"].is<JsonArrayConst>() ||
      !encoder["release"].is<JsonArrayConst>()) {
    error = tr("Encoder settings are missing.", "Encoder設定がありません。");
    return false;
  }
  candidate.rotationAddress = encoder["rotationAddress"].as<const char*>();
  candidate.rotationAddress.trim();
  candidate.sendIncrement = encoder["sendIncrement"] | false;
  candidate.absoluteInputMin = encoder["absoluteInputMin"].as<float>();
  candidate.absoluteInputMax = encoder["absoluteInputMax"].as<float>();
  candidate.incrementScale = encoder["incrementScale"].as<float>();
  candidate.outputMin = range["outMin"].as<float>();
  candidate.outputMax = range["outMax"].as<float>();
  const int outputType = range["type"].as<int>();
  if (!validJsonAddress(candidate.rotationAddress, error) ||
      !isfinite(candidate.absoluteInputMin) ||
      !isfinite(candidate.absoluteInputMax) ||
      !isfinite(candidate.incrementScale) || !isfinite(candidate.outputMin) ||
      !isfinite(candidate.outputMax) || outputType < TYPE_FLOAT ||
      outputType > TYPE_STRING) {
    if (error.isEmpty()) error = tr("Encoder number or output type is invalid.", "Encoderの数値または出力の型が正しくありません。");
    return false;
  }
  candidate.outputType = static_cast<ValueType>(outputType);
  candidate.clickMode = (encoder["clickMode"] | 0) == MODE_SEQUENCE
                            ? MODE_SEQUENCE : MODE_PRESS_RELEASE;
  JsonArrayConst press = encoder["press"].as<JsonArrayConst>();
  JsonArrayConst release = encoder["release"].as<JsonArrayConst>();
  if (press.size() + release.size() > MAX_KEY_OSC_MESSAGES) {
    error = tr("Click messages exceed the limit of 8.", "クリックメッセージが8件を超えています。");
    return false;
  }
  candidate.pressMessageCount = static_cast<uint8_t>(press.size());
  candidate.releaseMessageCount = static_cast<uint8_t>(release.size());
  uint8_t index = 0;
  for (JsonObjectConst message : press)
    if (!jsonMessage(message, candidate.pressMessages[index++], error)) return false;
  index = 0;
  for (JsonObjectConst message : release)
    if (!jsonMessage(message, candidate.releaseMessages[index++], error)) return false;
  return jsonSequence(encoder["sequence"].as<JsonObjectConst>(),
                      candidate.clickSequence, error);
}

bool angleSettingFromJson(JsonObjectConst object, AngleSetting& candidate,
                          bool includeIdentity, String& error) {
  if (object.isNull() || !object["deviceType"].is<int>() ||
      object["deviceType"].as<int>() != CHAIN_ANGLE_DEVICE_TYPE) {
    error = tr("Device type is not Angle.", "デバイス種類がAngleではありません。");
    return false;
  }
  if (includeIdentity) {
    if (!object["identity"].is<const char*>() ||
        !object["displayName"].is<const char*>()) {
      error = tr("Device identity or name is missing.", "デバイス識別子または名前がありません。");
      return false;
    }
    candidate.identity = object["identity"].as<const char*>();
    candidate.displayName = object["displayName"].as<const char*>();
    candidate.identity.trim();
    candidate.displayName.trim();
    if (!candidate.identity.startsWith("chain:") ||
        candidate.identity.length() <= 6 || candidate.displayName.isEmpty() ||
        candidate.displayName.length() > 64) {
      error = tr("Device identity or name is invalid.", "デバイス識別子または名前が正しくありません。");
      return false;
    }
  }
  JsonObjectConst angle = object["angle"].as<JsonObjectConst>();
  JsonObjectConst range = angle["range"].as<JsonObjectConst>();
  if (angle.isNull() || range.isNull() ||
      !angle["address"].is<const char*>() ||
      !angle["deadband"].is<int>() || !range.containsKey("outMin") ||
      !range.containsKey("outMax") || !range["type"].is<int>()) {
    error = tr("Angle settings are missing.", "Angle設定がありません。");
    return false;
  }
  candidate.address = angle["address"].as<const char*>();
  candidate.address.trim();
  candidate.use12Bit = angle["use12bit"] | true;
  candidate.deadband = angle["deadband"].as<int>();
  candidate.outputMin = range["outMin"].as<float>();
  candidate.outputMax = range["outMax"].as<float>();
  const int outputType = range["type"].as<int>();
  if (!validJsonAddress(candidate.address, error) || candidate.deadband < 1 ||
      !isfinite(candidate.outputMin) || !isfinite(candidate.outputMax) ||
      outputType < TYPE_FLOAT || outputType > TYPE_STRING) {
    if (error.isEmpty())
      error = tr("Angle values are invalid.", "Angleの設定値が正しくありません。");
    return false;
  }
  candidate.outputType = static_cast<ValueType>(outputType);
  return true;
}

bool tofSettingFromJson(JsonObjectConst object, TofSetting& candidate,
                        bool includeIdentity, String& error) {
  if (object.isNull() || (object["deviceType"] | -1) != CHAIN_TOF_DEVICE_TYPE) { error = tr("Device type is not ToF.", "デバイス種類がToFではありません。"); return false; }
  if (includeIdentity) {
    if (!object["identity"].is<const char*>() || !object["displayName"].is<const char*>()) { error = tr("Device identity or name is missing.", "デバイス識別子または名前がありません。"); return false; }
    candidate.identity = object["identity"].as<const char*>(); candidate.displayName = object["displayName"].as<const char*>();
    candidate.identity.trim(); candidate.displayName.trim();
    if (!candidate.identity.startsWith("chain:") || candidate.identity.length() <= 6 || candidate.displayName.isEmpty() || candidate.displayName.length() > 64) { error = tr("Device identity or name is invalid.", "デバイス識別子または名前が正しくありません。"); return false; }
  }
  JsonObjectConst tof = object["tof"].as<JsonObjectConst>(); JsonObjectConst range = tof["range"].as<JsonObjectConst>();
  if (tof.isNull() || range.isNull() || !tof["address"].is<const char*>() ||
      !tof["deadband"].is<int>() || !tof["maxDistanceMm"].is<int>() ||
      !range["outMin"].is<float>() || !range["outMax"].is<float>() ||
      !range["type"].is<int>()) {
    error = tr("ToF settings are missing.", "ToF設定がありません。");
    return false;
  }
  candidate.address = tof["address"].as<const char*>(); candidate.address.trim(); candidate.deadband = tof["deadband"].as<int>();
  candidate.maxDistanceMm = tof["maxDistanceMm"].as<int>(); candidate.nearValueHigh = tof["nearValueHigh"] | false;
  candidate.outputMin = range["outMin"].as<float>(); candidate.outputMax = range["outMax"].as<float>(); const int type = range["type"].as<int>();
  if (!validJsonAddress(candidate.address, error) || candidate.deadband < 1 || candidate.deadband > 2000 || candidate.maxDistanceMm < 31 || candidate.maxDistanceMm > 2000 || !isfinite(candidate.outputMin) || !isfinite(candidate.outputMax) || type < TYPE_FLOAT || type > TYPE_INT) { if (error.isEmpty()) error = tr("ToF values are invalid.", "ToFの設定値が正しくありません。"); return false; }
  candidate.outputType = static_cast<ValueType>(type); return true;
}

bool joystickSettingFromJson(JsonObjectConst object, JoystickSetting& candidate,
                             bool includeIdentity, String& error) {
  if (object.isNull() || (object["deviceType"] | -1) != CHAIN_JOYSTICK_DEVICE_TYPE) { error=tr("Device type is not Joystick.","デバイス種類がJoystickではありません。");return false; }
  if(includeIdentity){if(!object["identity"].is<const char*>()||!object["displayName"].is<const char*>()){error=tr("Device identity or name is missing.","デバイス識別子または名前がありません。");return false;}candidate.identity=object["identity"].as<const char*>();candidate.displayName=object["displayName"].as<const char*>();candidate.identity.trim();candidate.displayName.trim();if(!candidate.identity.startsWith("chain:")||candidate.identity.length()<=6||candidate.displayName.isEmpty()||candidate.displayName.length()>64){error=tr("Device identity or name is invalid.","デバイス識別子または名前が正しくありません。");return false;}}
  JsonObjectConst joy=object["joystick"].as<JsonObjectConst>(),range=joy["range"].as<JsonObjectConst>();
  if(joy.isNull()||range.isNull()||!joy["xAddress"].is<const char*>()||!joy["yAddress"].is<const char*>()||!joy["deadband"].is<int>()||!range["outMin"].is<float>()||!range["outMax"].is<float>()||!range["type"].is<int>()||!joy["press"].is<JsonArrayConst>()||!joy["release"].is<JsonArrayConst>()){error=tr("Joystick settings are missing.","Joystick設定がありません。");return false;}
  candidate.xAddress=joy["xAddress"].as<const char*>();candidate.yAddress=joy["yAddress"].as<const char*>();candidate.xAddress.trim();candidate.yAddress.trim();candidate.deadband=joy["deadband"].as<int>();candidate.invertX=joy["invertX"]|false;candidate.invertY=joy["invertY"]|false;candidate.outputMin=range["outMin"].as<float>();candidate.outputMax=range["outMax"].as<float>();int type=range["type"].as<int>();
  if(!validJsonAddress(candidate.xAddress,error)||!validJsonAddress(candidate.yAddress,error)||candidate.deadband<1||candidate.deadband>254||!isfinite(candidate.outputMin)||!isfinite(candidate.outputMax)||type<TYPE_FLOAT||type>TYPE_STRING)return false;candidate.outputType=(ValueType)type;candidate.clickMode=(joy["clickMode"]|0)==MODE_SEQUENCE?MODE_SEQUENCE:MODE_PRESS_RELEASE;
  JsonArrayConst press=joy["press"].as<JsonArrayConst>(),release=joy["release"].as<JsonArrayConst>();if(press.size()+release.size()>MAX_KEY_OSC_MESSAGES){error=tr("Click messages exceed the limit of 8.","クリックメッセージが8件を超えています。");return false;}candidate.pressMessageCount=press.size();candidate.releaseMessageCount=release.size();uint8_t i=0;for(JsonObjectConst m:press)if(!jsonMessage(m,candidate.pressMessages[i++],error))return false;i=0;for(JsonObjectConst m:release)if(!jsonMessage(m,candidate.releaseMessages[i++],error))return false;return jsonSequence(joy["sequence"].as<JsonObjectConst>(),candidate.clickSequence,error);
}

void sendProvisioningPage(const String& message = String()) {
  String html = pageStart("ChainOSCnano Wi-Fi Setup");
  html += F("<h1>ChainOSCnano Settings</h1><div class='card language-row'><h2>");
  html += tr("Language", "言語");
  html += F("</h2><form action='/set_language' method='post'><select name='language' onchange='this.form.submit()'><option value='en'");
  if (!isJapaneseUi()) html += F(" selected");
  html += F(">English</option><option value='ja'");
  if (isJapaneseUi()) html += F(" selected");
  html += F(">日本語</option></select></form></div><div class='card'><h2>");
  html += tr("ChainOSCnano Wi-Fi Setup", "ChainOSCnano Wi-Fi設定");
  html += F("</h2><p class='note'>");
  html += tr("Enter the Wi-Fi network used by your OSC device. Select a 2.4 GHz network.",
             "OSC送信先と同じWi-Fiを入力してください。2.4 GHz帯のネットワークを指定してください。");
  html += F("</p>");
  if (message.length() > 0) {
    html += F("<p class='status'>");
    html += message;
    html += F("</p>");
  }
  html += F("<form method='post' action='/save-wifi'>");
  html += F("<label for='ssid'>Wi-Fi SSID</label>");
  html += F("<input id='ssid' name='ssid' maxlength='32' required autocomplete='off'>");
  html += F("<label for='password'>Wi-Fi Password</label>");
  html += F("<input id='password' name='password' type='password' maxlength='64' autocomplete='off'>");
  html += F("<p class='note'>");
  html += tr("Use 8–63 characters, or a 64-digit hexadecimal PSK. Leave blank only for an open network.",
             "8～63文字、または64桁の16進数PSKを入力してください。オープンネットワークの場合のみ空欄にします。");
  html += F("</p><button type='submit'>");
  html += tr("Save Wi-Fi and Restart", "Wi-Fiを保存して再起動");
  html += F("</button></form></div>");
  sendPage(html);
}

String typeSelectHtml(const String& name, ValueType current,
                      bool validateValue = false) {
  String html = "<select class='type' name='" + name + "'";
  if (validateValue)
    html += " onchange=\"validateInput(this.closest('.osc-row').querySelector('.msg-value'))\"";
  html += ">";
  html += "<option value='0'" + String(current == TYPE_FLOAT ? " selected" : "") + ">Float</option>";
  html += "<option value='1'" + String(current == TYPE_INT ? " selected" : "") + ">Int</option>";
  html += "<option value='2'" + String(current == TYPE_STRING ? " selected" : "") + ">String</option></select>";
  return html;
}

String numericTypeSelectHtml(const String& name, ValueType current) {
  String html = "<select name='" + name + "'>";
  html += "<option value='0'" + String(current == TYPE_FLOAT ? " selected" : "") + ">Float</option>";
  html += "<option value='1'" + String(current == TYPE_INT ? " selected" : "") + ">Int</option></select>";
  return html;
}

String messageRowHtml(const String& group, const char* event, uint8_t order,
                      const KeyOscMessage& message) {
  const String prefix = String(event) == "press" ? "p" : "r";
  String html = "<div class='osc-row' data-group='" + group + "' data-event='" + event + "'>";
  html += "<div class='order'><button type='button' class='mv' onclick='moveMsg(this,-1)'>&uarr;</button><button type='button' class='mv' onclick='moveMsg(this,1)'>&darr;</button></div>";
  html += "<div class='field'><label>" + String(tr("OSC Address", "OSCアドレス")) + "</label><input class='msg-address' maxlength='192' required name='" + prefix + "_address_" + group + "_" + String(order) + "' value='" + htmlEscape(message.address) + "' oninput='limitAndValidate(this,192)'><small><span class='err'></span><span class='bytes'></span></small></div>";
  html += "<div class='field'><label>" + String(tr("Type", "型")) + "</label>" + typeSelectHtml(prefix + "_type_" + group + "_" + String(order), message.valueType, true) + "<small></small></div>";
  html += "<div class='field'><label>" + String(tr("Value", "値")) + "</label><input class='msg-value' maxlength='128' name='" + prefix + "_value_" + group + "_" + String(order) + "' value='" + htmlEscape(message.valueStr) + "' oninput='limitAndValidate(this,128)'><small><span class='err'></span><span class='bytes'></span></small></div>";
  html += "<button type='button' class='remove-msg' onclick='removeMsg(this)'>" + String(tr("Delete", "削除")) + "</button></div>";
  return html;
}

String pressReleaseHtml(const String& group, const KeySetting& setting,
                        bool sequenceMode) {
  String html = "<div id='pr-" + group + "' style='display:" + (sequenceMode ? "none" : "block") + "'>";
  html += "<div class='usage'><strong>" + String(tr("Messages", "メッセージ")) + " <span id='count-" + group + "'>" + String(setting.pressMessageCount + setting.releaseMessageCount) + "</span> / 8</strong><span>" + String(tr("Press + Release", "押した時＋離した時")) + "</span></div>";
  html += "<input type='hidden' id='p-count-" + group + "' name='p_count_" + group + "' value='" + String(setting.pressMessageCount) + "'><input type='hidden' id='r-count-" + group + "' name='r_count_" + group + "' value='" + String(setting.releaseMessageCount) + "'>";
  html += "<div class='event-tabs'><button type='button' class='event-tab active' onclick=\"showEvent('" + group + "','press',this)\">" + String(tr("Press", "押した時")) + "</button><button type='button' class='event-tab' onclick=\"showEvent('" + group + "','release',this)\">" + String(tr("Release", "離した時")) + "</button></div>";
  html += "<div class='event-panel' data-group='" + group + "' data-event='press'><div class='osc-list' id='list-press-" + group + "'>";
  for (uint8_t i = 0; i < setting.pressMessageCount; ++i)
    html += messageRowHtml(group, "press", i, setting.pressMessages[i]);
  html += "</div><div class='empty'>" + String(tr("No OSC message is sent when pressed.", "押したときはOSCメッセージを送信しません。")) + "</div><button type='button' class='add-msg' data-group='" + group + "' data-event='press' onclick='addMsg(this)'>" + String(tr("+ Add OSC Message", "+ OSCメッセージを追加")) + "</button></div>";
  html += "<div class='event-panel' data-group='" + group + "' data-event='release' style='display:none'><div class='osc-list' id='list-release-" + group + "'>";
  for (uint8_t i = 0; i < setting.releaseMessageCount; ++i)
    html += messageRowHtml(group, "release", i, setting.releaseMessages[i]);
  html += "</div><div class='empty'>" + String(tr("No OSC message is sent when released.", "離したときはOSCメッセージを送信しません。")) + "</div><button type='button' class='add-msg' data-group='" + group + "' data-event='release' onclick='addMsg(this)'>" + String(tr("+ Add OSC Message", "+ OSCメッセージを追加")) + "</button></div></div>";
  return html;
}

void appendKeyCard(String& html, const KeySetting& setting, size_t cardIndex) {
  const String collapseKey = String(cardIndex) + "-" + setting.identity;
  const String deviceLabel = setting.builtIn ? "M5NanoC6" : "Key";
  html += F("<div class='card device' data-device-index='");
  html += cardIndex;
  html += F("' data-collapse-key='");
  html += htmlEscape(collapseKey);
  html += F("'><div class='device-head'><h2><button id='collapse-");
  html += cardIndex;
  html += F("' class='collapse-button' type='button' aria-expanded='true' onclick=\"toggleDevice('");
  html += cardIndex;
  html += F("','");
  html += htmlEscape(collapseKey);
  html += F("')\">&#9660;</button><span class='badge badge-type'>");
  html += deviceLabel;
  html += F("</span> ");
  html += htmlEscape(setting.displayName);
  html += String(" <span class='badge badge-on'>") + tr("Connected", "接続済み") + "</span>";
  html += F("</h2>");
  html += F("<div class='device-menu-wrap'><button class='device-menu-button' type='button' aria-label='Device menu' onclick='toggleDeviceMenu(");
  html += cardIndex;
  html += F(")'>&hellip;</button><div id='device-menu-");
  html += cardIndex;
  html += F("' class='device-menu' hidden><button type='button' onclick='identifyDevice(");
  html += cardIndex;
  html += F(")'>");
  html += tr("Identify Device (Orange LED for 10s)", "デバイスを識別（LEDを10秒間オレンジ点灯）");
  html += F("</button><a href='/export_device_preset?index=");
  html += cardIndex;
  html += F("'>");
  html += tr("Export Preset (JSON)", "プリセットをエクスポート（JSON）");
  html += F("</a><button type='button' onclick='chooseDevicePreset(");
  html += cardIndex;
  html += F(")'>");
  html += tr("Import Preset (JSON)", "プリセットをインポート（JSON）");
  html += F("</button></div><input id='preset-file-");
  html += cardIndex;
  html += F("' type='file' accept='application/json,.json' hidden onchange='importDevicePreset(");
  html += cardIndex;
  html += F(",this)'></div>");
  html += F("</div><div id='device-body-");
  html += cardIndex;
  html += F("' class='device-body'><div class='uid'>");
  html += htmlEscape(setting.identity);
  html += F("</div><p id='preset-status-");
  html += cardIndex;
  html += F("' class='import-status'></p><input type='hidden' name='identity_");
  html += cardIndex;
  html += F("' value='");
  html += htmlEscape(setting.identity);
  html += F("'><input type='hidden' name='device_type_");
  html += cardIndex;
  html += F("' value='3'><div class='key-grid'><div><label>");
  html += tr("Device Name", "デバイス名");
  html += F("</label><input name='display_name_");
  html += cardIndex;
  html += F("' maxlength='64' required value='");
  html += htmlEscape(setting.displayName);
  html += F("'></div><div><label>");
  html += tr("Key Mode", "キーモード");
  html += F("</label><select name='mode_");
  html += cardIndex;
  html += F("' onchange=\"toggleKeyMode('pr-");
  html += cardIndex;
  html += F("','seq-");
  html += cardIndex;
  html += F("',this)\"><option value='0'");
  if (setting.mode == MODE_PRESS_RELEASE) html += F(" selected");
  html += F(">");
  html += tr("Press / Release", "押した時／離した時");
  html += F("</option><option value='1'");
  if (setting.mode == MODE_SEQUENCE) html += F(" selected");
  html += F(">");
  html += tr("Sequence", "シーケンス");
  html += F("</option></select></div></div>");
  html += pressReleaseHtml(String(cardIndex), setting, setting.mode == MODE_SEQUENCE);
  html += F("<div id='seq-");
  html += cardIndex;
  html += F("' class='sequence-card' style='display:");
  html += setting.mode == MODE_SEQUENCE ? F("block") : F("none");
  html += F("'><h3>");
  html += tr("Advance the value on each press", "押すたびに値を進める");
  html += F("</h3><p class='note'>");
  html += tr("Move from Start by Step and return to Start after End.", "開始値から増減量ずつ進み、終了値を超えると開始値へ戻ります。");
  html += F("</p><div class='seq-grid'><div class='address-field seq-address'><label>");
  html += tr("OSC Address", "OSCアドレス");
  html += F("</label><input class='osc-address' maxlength='192' required name='seq_address_");
  html += cardIndex;
  html += F("' value='");
  html += htmlEscape(setting.sequence.address);
  html += F("' oninput='limitAndValidate(this,192)'><small><span class='err'></span><span class='bytes'></span></small></div><div><label>");
  html += tr("Start", "開始値");
  html += F("</label><input type='number' step='any' required name='seq_start_"); html += cardIndex; html += F("' value='");
  html += String(setting.sequence.start, 7);
  html += F("'></div><div><label>"); html += tr("End", "終了値"); html += F("</label><input type='number' step='any' required name='seq_end_"); html += cardIndex; html += F("' value='");
  html += String(setting.sequence.end, 7);
  html += F("'></div><div><label>"); html += tr("Step", "増減量"); html += F("</label><input type='number' step='any' required name='seq_step_"); html += cardIndex; html += F("' value='");
  html += String(setting.sequence.step, 7);
  html += F("'></div><div><label>"); html += tr("Type", "型"); html += F("</label>");
  html += typeSelectHtml("seq_type_" + String(cardIndex), setting.sequence.valueType);
  html += F("</div></div></div></div></div>");
}

void appendEncoderCard(String& html, const EncoderSetting& setting,
                       size_t cardIndex) {
  const String idx = String(cardIndex);
  const String collapseKey = idx + "-" + setting.identity;
  KeySetting click;
  click.mode = setting.clickMode;
  click.pressMessageCount = setting.pressMessageCount;
  click.releaseMessageCount = setting.releaseMessageCount;
  click.sequence = setting.clickSequence;
  for (uint8_t i = 0; i < setting.pressMessageCount; ++i)
    click.pressMessages[i] = setting.pressMessages[i];
  for (uint8_t i = 0; i < setting.releaseMessageCount; ++i)
    click.releaseMessages[i] = setting.releaseMessages[i];

  html += "<div class='card device' data-device-index='" + idx +
          "' data-collapse-key='" + htmlEscape(collapseKey) + "'>";
  html += "<div class='device-head'><h2><button id='collapse-" + idx +
          "' class='collapse-button' type='button' aria-expanded='true' onclick=\"toggleDevice('" + idx + "','" + htmlEscape(collapseKey) + "')\">&#9660;</button><span class='badge badge-type'>Encoder</span> " + htmlEscape(setting.displayName) + " <span class='badge badge-on'>" + tr("Connected", "接続済み") + "</span></h2>";
  html += "<div class='device-menu-wrap'><button class='device-menu-button' type='button' aria-label='Device menu' onclick='toggleDeviceMenu(" + idx + ")'>&hellip;</button><div id='device-menu-" + idx + "' class='device-menu' hidden><button type='button' onclick='identifyDevice(" + idx + ")'>" + tr("Identify Device (Orange LED for 10s)", "デバイスを識別（LEDを10秒間オレンジ点灯）") + "</button><a href='/export_device_preset?index=" + idx + "'>" + tr("Export Preset (JSON)", "プリセットをエクスポート（JSON）") + "</a><button type='button' onclick='chooseDevicePreset(" + idx + ")'>" + tr("Import Preset (JSON)", "プリセットをインポート（JSON）") + "</button></div><input id='preset-file-" + idx + "' type='file' accept='application/json,.json' hidden onchange='importDevicePreset(" + idx + ",this)'></div></div>";
  html += "<div id='device-body-" + idx + "' class='device-body'><div class='uid'>" + htmlEscape(setting.identity) + "</div><p id='preset-status-" + idx + "' class='import-status'></p>";
  html += "<input type='hidden' name='identity_" + idx + "' value='" + htmlEscape(setting.identity) + "'><input type='hidden' name='device_type_" + idx + "' value='1'>";
  html += "<div class='key-grid'><div><label>" + String(tr("Device Name", "デバイス名")) + "</label><input name='display_name_" + idx + "' maxlength='64' required value='" + htmlEscape(setting.displayName) + "'></div></div>";
  html += "<div class='encoder-rotation'><h3>" + String(tr("Encoder Rotation", "エンコーダー回転")) + "</h3><div class='encoder-grid'>";
  html += "<div class='encoder-address address-field'><label>" + String(tr("OSC Address", "OSCアドレス")) + "</label><input class='osc-address' maxlength='192' required name='enc_rotation_" + idx + "' value='" + htmlEscape(setting.rotationAddress) + "' oninput='limitAndValidate(this,192)'><small><span class='err'></span><span class='bytes'></span></small></div>";
  html += "<div><label>" + String(tr("Mode", "モード")) + "</label><select name='enc_increment_" + idx + "' onchange='updateEncoderMode(this)'><option value='0'" + String(!setting.sendIncrement ? " selected" : "") + ">" + tr("Absolute", "絶対値") + "</option><option value='1'" + String(setting.sendIncrement ? " selected" : "") + ">" + tr("Increment", "増分") + "</option></select></div>";
  const String absHiddenClass = setting.sendIncrement ? " encoder-mode-hidden" : "";
  html += "<div class='encoder-absolute-setting" + absHiddenClass + "'><label>" + String(tr("Abs In Min", "絶対値入力の最小値")) + "</label><input type='number' step='any' name='enc_abs_min_" + idx + "' value='" + String(setting.absoluteInputMin, 7) + "'></div>";
  html += "<div class='encoder-absolute-setting" + absHiddenClass + "'><label>" + String(tr("Abs In Max", "絶対値入力の最大値")) + "</label><input type='number' step='any' name='enc_abs_max_" + idx + "' value='" + String(setting.absoluteInputMax, 7) + "'></div>";
  html += "<div><label>" + String(tr("Inc Scale", "増分倍率")) + "</label><input type='number' step='any' name='enc_scale_" + idx + "' value='" + String(setting.incrementScale, 7) + "'></div>";
  html += "<div><label>" + String(tr("Out Min", "出力最小値")) + "</label><input type='number' step='any' name='enc_out_min_" + idx + "' value='" + String(setting.outputMin, 7) + "'></div>";
  html += "<div><label>" + String(tr("Out Max", "出力最大値")) + "</label><input type='number' step='any' name='enc_out_max_" + idx + "' value='" + String(setting.outputMax, 7) + "'></div>";
  html += "<div><label>" + String(tr("Out Type", "出力の型")) + "</label>" + typeSelectHtml("enc_out_type_" + idx, setting.outputType) + "</div></div></div>";
  html += "<div class='click-section'><h3>" + String(tr("Encoder Click", "エンコーダークリック")) + "</h3><div class='key-grid'><div><label>" + String(tr("Click Mode", "クリックモード")) + "</label><select name='mode_" + idx + "' onchange=\"toggleKeyMode('pr-" + idx + "','seq-" + idx + "',this)\"><option value='0'" + String(setting.clickMode == MODE_PRESS_RELEASE ? " selected" : "") + ">" + tr("Press / Release", "押した時／離した時") + "</option><option value='1'" + String(setting.clickMode == MODE_SEQUENCE ? " selected" : "") + ">" + tr("Sequence", "シーケンス") + "</option></select></div></div>";
  html += pressReleaseHtml(idx, click,
                           setting.clickMode == MODE_SEQUENCE);
  html += "<div id='seq-" + idx + "' class='sequence-card' style='display:" + String(setting.clickMode == MODE_SEQUENCE ? "block" : "none") + "'><h3>" + String(tr("Click Sequence", "クリックシーケンス")) + "</h3><div class='seq-grid'>";
  html += "<div class='address-field seq-address'><label>" + String(tr("OSC Address", "OSCアドレス")) + "</label><input class='osc-address' maxlength='192' required name='seq_address_" + idx + "' value='" + htmlEscape(setting.clickSequence.address) + "' oninput='limitAndValidate(this,192)'><small><span class='err'></span><span class='bytes'></span></small></div>";
  html += "<div><label>" + String(tr("Start", "開始値")) + "</label><input type='number' step='any' required name='seq_start_" + idx + "' value='" + String(setting.clickSequence.start, 7) + "'></div><div><label>" + String(tr("End", "終了値")) + "</label><input type='number' step='any' required name='seq_end_" + idx + "' value='" + String(setting.clickSequence.end, 7) + "'></div>";
  html += "<div><label>" + String(tr("Step", "増減量")) + "</label><input type='number' step='any' required name='seq_step_" + idx + "' value='" + String(setting.clickSequence.step, 7) + "'></div><div><label>" + String(tr("Type", "型")) + "</label>" + typeSelectHtml("seq_type_" + idx, setting.clickSequence.valueType) + "</div></div></div></div></div></div>";
}

String savedDeviceStatusBadge(uint8_t connectedPortMask) {
  if (connectedPortMask != 0) {
    return String(F(" <span class='badge badge-on'>")) +
           tr("Connected", "接続済み") + F("</span>");
  }
  return String(F(" <span class='badge badge-off'>")) +
         tr("Not Connected", "未接続") + F("</span>");
}

String savedDeviceDeleteForm(const String& identity, int deviceType,
                             uint8_t connectedPortMask) {
  if (connectedPortMask != 0) return String();
  String html = F("<form method='post' action='/delete_device' onsubmit='deleteSavedDevice(event,this);return false'><input type='hidden' name='identity' value='");
  html += htmlEscape(identity);
  html += F("'>");
  if (deviceType != CHAIN_KEY_DEVICE_TYPE) {
    html += F("<input type='hidden' name='device_type' value='");
    html += deviceType;
    html += F("'>");
  }
  html += F("<button class='btn-warning' type='submit'>");
  html += tr("Delete Settings", "設定を削除");
  html += F("</button></form>");
  return html;
}

void appendSavedDeviceCard(String& html, const KeySetting& setting) {
  String uid = setting.identity.startsWith("chain:")
                   ? setting.identity.substring(6) : setting.identity;
  html += F("<div class='card saved-device-card'><h2><span class='badge badge-type'>Key</span> ");
  html += htmlEscape(setting.displayName);
  html += savedDeviceStatusBadge(setting.connectedPortMask);
  html += F("</h2><p class='meta'>");
  html += tr("Type: ", "種類: ");
  html += F("<strong>Key</strong></p><div class='uid'>");
  html += htmlEscape(uid);
  html += F("</div>");
  if (setting.connectedPortMask == 0) {
    html += F("<form method='post' action='/delete_device' onsubmit='deleteSavedDevice(event,this);return false'><input type='hidden' name='identity' value='");
    html += htmlEscape(setting.identity);
    html += F("'><button class='btn-warning' type='submit'>");
    html += tr("Delete Settings", "設定を削除");
    html += F("</button></form>");
  }
  html += F("</div>");
}

void appendSavedEncoderCard(String& html, const EncoderSetting& setting) {
  const String uid = setting.identity.startsWith("chain:")
                         ? setting.identity.substring(6) : setting.identity;
  html += "<div class='card saved-device-card'><h2><span class='badge badge-type'>Encoder</span> " + htmlEscape(setting.displayName) + savedDeviceStatusBadge(setting.connectedPortMask) + "</h2><p class='meta'>" + tr("Type: ", "種類: ") + "<strong>Encoder</strong></p><div class='uid'>" + htmlEscape(uid) + "</div>";
  html += savedDeviceDeleteForm(setting.identity, CHAIN_ENCODER_DEVICE_TYPE,
                                setting.connectedPortMask);
  html += F("</div>");
}

void appendAngleCard(String& html, const AngleSetting& setting,
                     size_t cardIndex) {
  const String idx = String(cardIndex);
  const String collapseKey = idx + "-" + setting.identity;
  html += "<div class='card device' data-device-index='" + idx +
          "' data-collapse-key='" + htmlEscape(collapseKey) + "'>";
  html += "<div class='device-head'><h2><button id='collapse-" + idx +
          "' class='collapse-button' type='button' aria-expanded='true' onclick=\"toggleDevice('" + idx + "','" + htmlEscape(collapseKey) + "')\">&#9660;</button><span class='badge badge-type'>Angle</span> " + htmlEscape(setting.displayName) + " <span class='badge badge-on'>" + tr("Connected", "接続済み") + "</span></h2>";
  html += "<div class='device-menu-wrap'><button class='device-menu-button' type='button' aria-label='Device menu' onclick='toggleDeviceMenu(" + idx + ")'>&hellip;</button><div id='device-menu-" + idx + "' class='device-menu' hidden><button type='button' onclick='identifyDevice(" + idx + ")'>" + tr("Identify Device (Orange LED for 10s)", "デバイスを識別（LEDを10秒間オレンジ点灯）") + "</button><a href='/export_device_preset?index=" + idx + "'>" + tr("Export Preset (JSON)", "プリセットをエクスポート（JSON）") + "</a><button type='button' onclick='chooseDevicePreset(" + idx + ")'>" + tr("Import Preset (JSON)", "プリセットをインポート（JSON）") + "</button></div><input id='preset-file-" + idx + "' type='file' accept='application/json,.json' hidden onchange='importDevicePreset(" + idx + ",this)'></div></div>";
  html += "<div id='device-body-" + idx + "' class='device-body'><div class='uid'>" + htmlEscape(setting.identity) + "</div><p id='preset-status-" + idx + "' class='import-status'></p>";
  html += "<input type='hidden' name='identity_" + idx + "' value='" + htmlEscape(setting.identity) + "'><input type='hidden' name='device_type_" + idx + "' value='2'>";
  html += "<div class='key-grid'><div><label>" + String(tr("Device Name", "デバイス名")) + "</label><input name='display_name_" + idx + "' maxlength='64' required value='" + htmlEscape(setting.displayName) + "'></div></div>";
  html += "<div class='angle-section'><h3>Angle</h3><div class='angle-grid'>";
  html += "<div class='address-field angle-address'><label>" + String(tr("OSC Address", "OSCアドレス")) + "</label><input class='osc-address' maxlength='192' required name='angle_address_" + idx + "' value='" + htmlEscape(setting.address) + "' oninput='limitAndValidate(this,192)'><small><span class='err'></span><span class='bytes'></span></small></div>";
  html += "<div><label>" + String(tr("Resolution", "分解能")) + "</label><select name='angle_12bit_" + idx + "'><option value='1'" + String(setting.use12Bit ? " selected" : "") + ">12-bit</option><option value='0'" + String(!setting.use12Bit ? " selected" : "") + ">8-bit</option></select></div>";
  html += "<div><label>" + String(tr("Deadband", "不感帯")) + "</label><input type='number' min='1' required name='angle_deadband_" + idx + "' value='" + String(setting.deadband) + "'></div>";
  html += "<div><label>" + String(tr("Out Min", "出力最小値")) + "</label><input type='number' step='any' required name='angle_out_min_" + idx + "' value='" + String(setting.outputMin, 7) + "'></div>";
  html += "<div><label>" + String(tr("Out Max", "出力最大値")) + "</label><input type='number' step='any' required name='angle_out_max_" + idx + "' value='" + String(setting.outputMax, 7) + "'></div>";
  html += "<div><label>" + String(tr("Out Type", "出力の型")) + "</label>" + typeSelectHtml("angle_out_type_" + idx, setting.outputType) + "</div></div></div></div></div>";
}

void appendSavedAngleCard(String& html, const AngleSetting& setting) {
  const String uid = setting.identity.startsWith("chain:")
                         ? setting.identity.substring(6) : setting.identity;
  html += "<div class='card saved-device-card'><h2><span class='badge badge-type'>Angle</span> " + htmlEscape(setting.displayName) + savedDeviceStatusBadge(setting.connectedPortMask) + "</h2><p class='meta'>" + tr("Type: ", "種類: ") + "<strong>Angle</strong></p><div class='uid'>" + htmlEscape(uid) + "</div>";
  html += savedDeviceDeleteForm(setting.identity, CHAIN_ANGLE_DEVICE_TYPE,
                                setting.connectedPortMask);
  html += F("</div>");
}

void appendTofCard(String& html, const TofSetting& setting, size_t cardIndex) {
  const String idx = String(cardIndex), collapseKey = idx + "-" + setting.identity;
  html += "<div class='card device' data-device-index='" + idx + "' data-collapse-key='" + htmlEscape(collapseKey) + "'><div class='device-head'><h2><button id='collapse-" + idx + "' class='collapse-button' type='button' aria-expanded='true' onclick=\"toggleDevice('" + idx + "','" + htmlEscape(collapseKey) + "')\">&#9660;</button><span class='badge badge-type'>ToF</span> " + htmlEscape(setting.displayName) + " <span class='badge badge-on'>" + tr("Connected", "接続済み") + "</span></h2>";
  html += "<div class='device-menu-wrap'><button class='device-menu-button' type='button' aria-label='Device menu' onclick='toggleDeviceMenu(" + idx + ")'>&hellip;</button><div id='device-menu-" + idx + "' class='device-menu' hidden><button type='button' onclick='identifyDevice(" + idx + ")'>" + tr("Identify Device (Orange LED for 10s)", "デバイスを識別（LEDを10秒間オレンジ点灯）") + "</button><a href='/export_device_preset?index=" + idx + "'>" + tr("Export Preset (JSON)", "プリセットをエクスポート（JSON）") + "</a><button type='button' onclick='chooseDevicePreset(" + idx + ")'>" + tr("Import Preset (JSON)", "プリセットをインポート（JSON）") + "</button></div><input id='preset-file-" + idx + "' type='file' accept='application/json,.json' hidden onchange='importDevicePreset(" + idx + ",this)'></div></div>";
  html += "<div id='device-body-" + idx + "' class='device-body'><div class='uid'>" + htmlEscape(setting.identity) + "</div><p id='preset-status-" + idx + "' class='import-status'></p><input type='hidden' name='identity_" + idx + "' value='" + htmlEscape(setting.identity) + "'><input type='hidden' name='device_type_" + idx + "' value='5'>";
  html += "<div class='key-grid'><div><label>" + String(tr("Device Name", "デバイス名")) + "</label><input name='display_name_" + idx + "' maxlength='64' required value='" + htmlEscape(setting.displayName) + "'></div></div>";
  html += "<div class='tof-section'><h3>" + String(tr("ToF Distance (mm)", "ToF距離 (mm)")) + "</h3><div class='tof-grid'>";
  html += "<div class='address-field tof-address'><label>" + String(tr("OSC Address", "OSCアドレス")) + "</label><input class='osc-address' maxlength='192' required name='tof_address_" + idx + "' value='" + htmlEscape(setting.address) + "' oninput='limitAndValidate(this,192)'><small><span class='err'></span><span class='bytes'></span></small></div>";
  html += "<div><label>" + String(tr("Deadband (mm)", "不感帯 (mm)")) + "</label><input type='number' min='1' max='2000' required name='tof_deadband_" + idx + "' value='" + String(setting.deadband) + "'></div>";
  html += "<div><label>" + String(tr("Maximum Distance (mm)", "最大距離 (mm)")) + "</label><input type='number' min='31' max='2000' required name='tof_max_" + idx + "' value='" + String(setting.maxDistanceMm) + "'></div>";
  html += "<div><label>" + String(tr("Direction", "変換方向")) + "</label><select name='tof_near_high_" + idx + "'><option value='0'" + String(!setting.nearValueHigh ? " selected" : "") + ">" + tr("Near → Out Min / Far → Out Max", "近い → 出力最小値／遠い → 出力最大値") + "</option><option value='1'" + String(setting.nearValueHigh ? " selected" : "") + ">" + tr("Near → Out Max / Far → Out Min", "近い → 出力最大値／遠い → 出力最小値") + "</option></select></div>";
  html += "<div><label>" + String(tr("Out Min", "出力最小値")) + "</label><input type='number' step='any' required name='tof_out_min_" + idx + "' value='" + String(setting.outputMin, 7) + "'></div><div><label>" + String(tr("Out Max", "出力最大値")) + "</label><input type='number' step='any' required name='tof_out_max_" + idx + "' value='" + String(setting.outputMax, 7) + "'></div><div><label>" + String(tr("Out Type", "出力の型")) + "</label>" + numericTypeSelectHtml("tof_out_type_" + idx, setting.outputType) + "</div>";
  html += "<p class='note tof-address'>" + String(tr("OSC is sent only while the measured distance is 30 mm or more and less than Maximum Distance.", "測定距離が30 mm以上かつ最大距離未満の間だけOSCを送信します。")) + "</p></div></div></div></div>";
}

void appendSavedTofCard(String& html, const TofSetting& setting) {
  const String uid = setting.identity.startsWith("chain:") ? setting.identity.substring(6) : setting.identity;
  html += "<div class='card saved-device-card'><h2><span class='badge badge-type'>ToF</span> " + htmlEscape(setting.displayName) + savedDeviceStatusBadge(setting.connectedPortMask) + "</h2><p class='meta'>" + tr("Type: ", "種類: ") + "<strong>ToF</strong></p><div class='uid'>" + htmlEscape(uid) + "</div>";
  html += savedDeviceDeleteForm(setting.identity, CHAIN_TOF_DEVICE_TYPE,
                                setting.connectedPortMask);
  html += F("</div>");
}

void appendJoystickCard(String& html, const JoystickSetting& setting, size_t cardIndex) {
  const String idx=String(cardIndex),collapseKey=idx+"-"+setting.identity;KeySetting click;click.mode=setting.clickMode;click.pressMessageCount=setting.pressMessageCount;click.releaseMessageCount=setting.releaseMessageCount;click.sequence=setting.clickSequence;for(uint8_t i=0;i<setting.pressMessageCount;++i)click.pressMessages[i]=setting.pressMessages[i];for(uint8_t i=0;i<setting.releaseMessageCount;++i)click.releaseMessages[i]=setting.releaseMessages[i];
  html+="<div class='card device' data-device-index='"+idx+"' data-collapse-key='"+htmlEscape(collapseKey)+"'><div class='device-head'><h2><button id='collapse-"+idx+"' class='collapse-button' type='button' aria-expanded='true' onclick=\"toggleDevice('"+idx+"','"+htmlEscape(collapseKey)+"')\">&#9660;</button><span class='badge badge-type'>Joystick</span> "+htmlEscape(setting.displayName)+" <span class='badge badge-on'>"+tr("Connected","接続済み")+"</span></h2>";
  html+="<div class='device-menu-wrap'><button class='device-menu-button' type='button' onclick='toggleDeviceMenu("+idx+")'>&hellip;</button><div id='device-menu-"+idx+"' class='device-menu' hidden><button type='button' onclick='identifyDevice("+idx+")'>"+tr("Identify Device (Orange LED for 10s)","デバイスを識別（LEDを10秒間オレンジ点灯）")+"</button><a href='/export_device_preset?index="+idx+"'>"+tr("Export Preset (JSON)","プリセットをエクスポート（JSON）")+"</a><button type='button' onclick='chooseDevicePreset("+idx+")'>"+tr("Import Preset (JSON)","プリセットをインポート（JSON）")+"</button></div><input id='preset-file-"+idx+"' type='file' accept='application/json,.json' hidden onchange='importDevicePreset("+idx+",this)'></div></div>";
  html+="<div id='device-body-"+idx+"' class='device-body'><div class='uid'>"+htmlEscape(setting.identity)+"</div><p id='preset-status-"+idx+"' class='import-status'></p><input type='hidden' name='identity_"+idx+"' value='"+htmlEscape(setting.identity)+"'><input type='hidden' name='device_type_"+idx+"' value='4'><div class='key-grid'><div><label>"+tr("Device Name","デバイス名")+"</label><input name='display_name_"+idx+"' maxlength='64' required value='"+htmlEscape(setting.displayName)+"'></div></div>";
  html+="<div class='joystick-section'><h3>"+String(tr("Joystick XY","ジョイスティック XY"))+"</h3><div class='joystick-grid'>";
  html+="<div class='address-field joystick-address'><label>"+String(tr("X Address","X軸OSCアドレス"))+"</label><input class='osc-address' maxlength='192' required name='joy_x_"+idx+"' value='"+htmlEscape(setting.xAddress)+"' oninput='limitAndValidate(this,192)'><small><span class='err'></span><span class='bytes'></span></small></div>";
  html+="<div class='address-field joystick-address'><label>"+String(tr("Y Address","Y軸OSCアドレス"))+"</label><input class='osc-address' maxlength='192' required name='joy_y_"+idx+"' value='"+htmlEscape(setting.yAddress)+"' oninput='limitAndValidate(this,192)'><small><span class='err'></span><span class='bytes'></span></small></div>";
  html+="<div class='joystick-invert'><label><input type='checkbox' name='joy_inv_x_"+idx+"' value='1'"+String(setting.invertX?" checked":"")+"><span>"+tr("Invert X (+/-)","X軸反転 (+/-)")+"</span></label><label><input type='checkbox' name='joy_inv_y_"+idx+"' value='1'"+String(setting.invertY?" checked":"")+"><span>"+tr("Invert Y (+/-)","Y軸反転 (+/-)")+"</span></label></div>";
  html+="<div><label>"+String(tr("Deadband","不感帯"))+"</label><input type='number' min='1' max='254' name='joy_deadband_"+idx+"' value='"+String(setting.deadband)+"'></div><div><label>"+String(tr("Out Min","出力最小値"))+"</label><input type='number' step='any' name='joy_out_min_"+idx+"' value='"+String(setting.outputMin,7)+"'></div><div><label>"+String(tr("Out Max","出力最大値"))+"</label><input type='number' step='any' name='joy_out_max_"+idx+"' value='"+String(setting.outputMax,7)+"'></div><div><label>"+String(tr("Out Type","出力の型"))+"</label>"+typeSelectHtml("joy_out_type_"+idx,setting.outputType)+"</div></div></div>";
  html+="<div class='click-section'><h3>"+String(tr("Joystick Click","ジョイスティッククリック"))+"</h3><div class='key-grid'><div><label>"+String(tr("Click Mode","クリックモード"))+"</label><select name='mode_"+idx+"' onchange=\"toggleKeyMode('pr-"+idx+"','seq-"+idx+"',this)\"><option value='0'"+String(setting.clickMode==MODE_PRESS_RELEASE?" selected":"")+">"+tr("Press / Release","押した時／離した時")+"</option><option value='1'"+String(setting.clickMode==MODE_SEQUENCE?" selected":"")+">"+tr("Sequence","シーケンス")+"</option></select></div></div>"+pressReleaseHtml(idx,click,setting.clickMode==MODE_SEQUENCE);
  html+="<div id='seq-"+idx+"' class='sequence-card' style='display:"+String(setting.clickMode==MODE_SEQUENCE?"block":"none")+"'><h3>"+String(tr("Click Sequence","クリックシーケンス"))+"</h3><div class='seq-grid'><div class='address-field seq-address'><label>"+String(tr("OSC Address","OSCアドレス"))+"</label><input class='osc-address' maxlength='192' required name='seq_address_"+idx+"' value='"+htmlEscape(setting.clickSequence.address)+"' oninput='limitAndValidate(this,192)'><small><span class='err'></span><span class='bytes'></span></small></div><div><label>"+tr("Start","開始値")+"</label><input type='number' step='any' name='seq_start_"+idx+"' value='"+String(setting.clickSequence.start,7)+"'></div><div><label>"+tr("End","終了値")+"</label><input type='number' step='any' name='seq_end_"+idx+"' value='"+String(setting.clickSequence.end,7)+"'></div><div><label>"+tr("Step","増減量")+"</label><input type='number' step='any' name='seq_step_"+idx+"' value='"+String(setting.clickSequence.step,7)+"'></div><div><label>"+tr("Type","型")+"</label>"+typeSelectHtml("seq_type_"+idx,setting.clickSequence.valueType)+"</div></div></div></div></div></div>";
}

void appendSavedJoystickCard(String& html, const JoystickSetting& setting) {
  const String uid = setting.identity.startsWith("chain:")
                         ? setting.identity.substring(6) : setting.identity;
  html += "<div class='card saved-device-card'><h2><span class='badge badge-type'>Joystick</span> " +
          htmlEscape(setting.displayName) +
          savedDeviceStatusBadge(setting.connectedPortMask) +
          "</h2><p class='meta'>" + tr("Type: ", "種類: ") +
          "<strong>Joystick</strong></p><div class='uid'>" +
          htmlEscape(uid) + "</div>";
  html += savedDeviceDeleteForm(setting.identity, CHAIN_JOYSTICK_DEVICE_TYPE,
                                setting.connectedPortMask);
  html += F("</div>");
}

void sendStatusPage(const String& message = String()) {
#if CHAINOSCNANO_WEB_PERF_DEBUG
  const uint32_t requestId = ++webRequestSequence;
  const uint32_t requestStartedMs = millis();
  logWebPerf(requestId, requestStartedMs, "BEGIN", 0, 0, 0);
#endif
  String html = pageStart("ChainOSCnano Settings");
  html += F("<div id='save-toast' class='toast' hidden></div><h1>ChainOSCnano Settings</h1>");
  html += F("<div class='card language-row'><h2>"); html += tr("Language", "言語");
  html += F("</h2><form action='/set_language' method='post'><select name='language' onchange='this.form.submit()'><option value='en'");
  if (!isJapaneseUi()) html += F(" selected");
  html += F(">English</option><option value='ja'");
  if (isJapaneseUi()) html += F(" selected");
  html += F(">日本語</option></select></form></div>");
  html += F("<div class='card'><h2>"); html += tr("System", "システム");
  html += F("</h2><p class='status'>"); html += tr("Wi-Fi connected", "Wi-Fi接続済み");
  html += F("</p><div class='system-grid'><div class='system-item'><strong>");
  html += tr("Product", "製品名");
  html += F("</strong><code>ChainOSCnano</code></div><div class='system-item'><strong>Version</strong>");
  html += APP_VERSION;
  html += F("</div><div class='system-item'><strong>"); html += tr("IP Address", "IPアドレス"); html += F("</strong><code>");
  html += WiFi.localIP().toString();
  html += F("</code></div><div class='system-item'><strong>mDNS</strong><code>http://");
  html += WIFI_MDNS_HOST;
  html += F(".local/</code></div></div></div>");
  html += F("<div class='card'><h2>WiFi</h2><p class='meta'>IP: ");
  html += WiFi.localIP().toString();
  html += F("</p><form method='post' action='/forget-wifi' onsubmit=\"return confirm('");
  html += tr("Delete WiFi settings?", "Wi-Fi設定を削除しますか？");
  html += F("')\"><button class='danger' type='submit'>");
  html += tr("Delete WiFi Settings", "Wi-Fi設定を削除");
  html += F("</button></form></div>");
  html += F("<div class='card backup-tools'><h2>");
  html += tr("Settings Backup &amp; Restore", "設定のバックアップと復元");
  html += F("</h2><p class='note'>");
  html += tr("Back up or restore all ChainOSCnano settings as versioned JSON. WiFi credentials are not included.", "ChainOSCnanoの全設定をバージョン付きJSONでバックアップ・復元します。Wi-Fi認証情報は含まれません。");
  html += F("</p><div class='tool-row'><a href='/export_settings'>");
  html += tr("Export Settings (JSON)", "設定をエクスポート（JSON）");
  html += F("</a><button type='button' onclick='chooseSettingsFile()'>");
  html += tr("Import Settings (JSON)", "設定をインポート（JSON）");
  html += F("</button></div><input id='settings-import-file' type='file' accept='application/json,.json' hidden onchange='importSettings(this)'><p id='settings-import-status' class='import-status'></p></div>");
  if (!message.isEmpty()) {
    html += F("<p class='status'>");
    html += htmlEscape(message);
    html += F("</p>");
  }
  html += F("<form id='settings-form' method='post' action='/save-all' onsubmit='saveSettings(event);return false'><div class='card'><h2>"); html += tr("OSC Destination", "OSC送信先"); html += F("</h2>");
  html += F("<label for='osc_host'>"); html += tr("Hostname or IPv4 address", "ホスト名またはIPv4アドレス"); html += F("</label>");
  html += F("<input id='osc_host' name='osc_host' maxlength='253' required value='");
  html += htmlEscape(oscTargetHost());
  html += F("'>");
  html += F("<label for='osc_port'>"); html += tr("UDP Port", "UDPポート"); html += F("</label>");
  html += F("<input id='osc_port' name='osc_port' type='number' min='1' max='65535' required value='");
  html += oscTargetPort();
  html += F("'></div>");
  html += F("<h2 class='section-title'>"); html += tr("Connected Devices", "接続中のデバイス"); html += F("</h2>");

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", "");
  size_t sentCards = 0;
  auto flushHtml = [&](const char* phase) -> bool {
    const size_t bytes = html.length();
    if (!server.client().connected()) {
#if CHAINOSCNANO_WEB_PERF_DEBUG
      logWebPerf(requestId, requestStartedMs, phase, bytes, 0, sentCards);
#endif
      html.remove(0);
      return false;
    }
    const uint32_t operationStartedMs = millis();
    server.sendContent(html);
    const uint32_t operationMs = millis() - operationStartedMs;
    html.remove(0);
    yield();
#if CHAINOSCNANO_WEB_PERF_DEBUG
    logWebPerf(requestId, requestStartedMs, phase, bytes, operationMs,
               sentCards);
#endif
    return server.client().connected();
  };
  if (!flushHtml("COMMON_SENT")) return;

  size_t cardIndex = 0;
  // The M5NanoC6 body button is a fixed, always-connected Key. Render it
  // before the external Chain devices, matching ChainOSCmini's built-in keys.
  for (size_t index = 0; index < keySettingsCount(); ++index) {
    KeySetting* setting = keySettingsAt(index);
    if (setting != nullptr && setting->builtIn) {
      appendKeyCard(html, *setting, cardIndex++);
      ++sentCards;
      if (!flushHtml("DEVICE_SENT")) return;
    }
  }
  // Preserve the physical order reported by the NanoC6 GPIO1/2 Chain port.
  const size_t connectedChainCount = chainProbeConnectedDeviceCount();
  for (size_t physicalIndex = 0; physicalIndex < connectedChainCount;
       ++physicalIndex) {
    String identity;
    uint8_t deviceType = 0;
    if (!chainProbeConnectedDeviceAt(physicalIndex, identity, deviceType)) {
      continue;
    }

    bool appended = false;
    if (deviceType == 3) {  // Chain Key
      for (size_t index = 0; index < keySettingsCount(); ++index) {
        KeySetting* setting = keySettingsAt(index);
        if (setting != nullptr && !setting->builtIn &&
            setting->connectedPortMask != 0 &&
            setting->identity == identity) {
          appendKeyCard(html, *setting, cardIndex++);
          appended = true;
          break;
        }
      }
    } else if (deviceType == 1) {  // Chain Encoder
      for (size_t index = 0; index < encoderSettingsCount(); ++index) {
        EncoderSetting* setting = encoderSettingsAt(index);
        if (setting != nullptr && setting->connectedPortMask != 0 &&
            setting->identity == identity) {
          appendEncoderCard(html, *setting, cardIndex++);
          appended = true;
          break;
        }
      }
    } else if (deviceType == 2) {  // Chain Angle
      for (size_t index = 0; index < angleSettingsCount(); ++index) {
        AngleSetting* setting = angleSettingsAt(index);
        if (setting != nullptr && setting->connectedPortMask != 0 &&
            setting->identity == identity) {
          appendAngleCard(html, *setting, cardIndex++);
          appended = true;
          break;
        }
      }
    } else if (deviceType == 5) {  // Chain ToF
      for (size_t index = 0; index < tofSettingsCount(); ++index) {
        TofSetting* setting = tofSettingsAt(index);
        if (setting != nullptr && setting->connectedPortMask != 0 &&
            setting->identity == identity) {
          appendTofCard(html, *setting, cardIndex++);
          appended = true;
          break;
        }
      }
    } else if (deviceType == 4) {  // Chain Joystick
      for (size_t index = 0; index < joystickSettingsCount(); ++index) {
        JoystickSetting* setting = joystickSettingsAt(index);
        if (setting != nullptr && setting->connectedPortMask != 0 &&
            setting->identity == identity) {
          appendJoystickCard(html, *setting, cardIndex++);
          appended = true;
          break;
        }
      }
    }

    if (appended) {
      ++sentCards;
      if (!flushHtml("DEVICE_SENT")) return;
    }
  }
  html += F("<input type='hidden' name='connected_count' value='");
  html += cardIndex;
  html += F("'><div class='save-bar'><span id='dirty-status' class='dirty-status' hidden>");
  html += tr("Unsaved changes", "未保存の変更があります");
  html += F("</span><button class='primary' type='submit'>");
  html += tr("Save All Settings", "すべての設定を保存");
  html += F("</button></div></form>");
  html += F("<div class='card saved-settings'><h2>");
  html += tr("Saved Device Settings", "保存済みデバイス設定");
  html += F("</h2><p class='note'>");
  html += tr("Only devices whose settings have been saved are shown.", "設定を保存したデバイスのみ表示します。");
  html += F("</p></div>");
  if (!flushHtml("SAVED_HEADER_SENT")) return;
  for (size_t index = 0; index < keySettingsCount(); ++index) {
    KeySetting* setting = keySettingsAt(index);
    if (setting != nullptr && !setting->builtIn) {
      appendSavedDeviceCard(html, *setting);
      if (!flushHtml("SAVED_DEVICE_SENT")) return;
    }
  }
  for (size_t index = 0; index < encoderSettingsCount(); ++index) {
    EncoderSetting* setting = encoderSettingsAt(index);
    if (setting != nullptr) {
      appendSavedEncoderCard(html, *setting);
      if (!flushHtml("SAVED_DEVICE_SENT")) return;
    }
  }
  for (size_t index = 0; index < angleSettingsCount(); ++index) {
    AngleSetting* setting = angleSettingsAt(index);
    if (setting != nullptr) {
      appendSavedAngleCard(html, *setting);
      if (!flushHtml("SAVED_DEVICE_SENT")) return;
    }
  }
  for (size_t index = 0; index < tofSettingsCount(); ++index) {
    TofSetting* setting = tofSettingsAt(index);
    if (setting != nullptr) {
      appendSavedTofCard(html, *setting);
      if (!flushHtml("SAVED_DEVICE_SENT")) return;
    }
  }
  for (size_t index = 0; index < joystickSettingsCount(); ++index) {
    JoystickSetting* setting = joystickSettingsAt(index);
    if (setting) {
      appendSavedJoystickCard(html, *setting);
      if (!flushHtml("SAVED_DEVICE_SENT")) return;
    }
  }
  html += F("</main></body></html>");
  if (!flushHtml("FOOTER_SENT")) return;
  server.sendContent("");
#if CHAINOSCNANO_WEB_PERF_DEBUG
  logWebPerf(requestId, requestStartedMs, "END", 0, 0, sentCards);
#endif
}

bool parseInt32(const String& text, int32_t& value) {
  if (text.isEmpty()) return false;
  size_t index = text[0] == '-' ? 1 : 0;
  if (index == text.length()) return false;
  for (; index < text.length(); ++index) {
    if (!isdigit(static_cast<unsigned char>(text[index]))) return false;
  }
  const long long parsed = strtoll(text.c_str(), nullptr, 10);
  if (parsed < INT32_MIN || parsed > INT32_MAX) return false;
  value = static_cast<int32_t>(parsed);
  return true;
}

bool readKeySetting(size_t formIndex, KeySetting& candidate) {
  const String suffix = "_" + String(formIndex);
  const String identity = server.arg("identity" + suffix);
  KeySetting* current = nullptr;
  for (size_t index = 0; index < keySettingsCount(); ++index) {
    KeySetting* setting = keySettingsAt(index);
    if (setting != nullptr && setting->identity == identity &&
        (setting->builtIn || setting->connectedPortMask != 0)) {
      current = setting;
      break;
    }
  }
  if (current == nullptr) return false;
  candidate = *current;
  candidate.displayName = server.arg("display_name" + suffix);
  candidate.displayName.trim();
  candidate.mode = server.arg("mode" + suffix).toInt() == MODE_SEQUENCE
                       ? MODE_SEQUENCE : MODE_PRESS_RELEASE;
  const int pressCount = server.arg("p_count" + suffix).toInt();
  const int releaseCount = server.arg("r_count" + suffix).toInt();
  if (candidate.displayName.isEmpty() || pressCount < 0 || releaseCount < 0 ||
      pressCount + releaseCount > MAX_KEY_OSC_MESSAGES) return false;
  candidate.pressMessageCount = pressCount;
  candidate.releaseMessageCount = releaseCount;

  bool valid = true;
  for (uint8_t index = 0; valid && index < candidate.pressMessageCount; ++index) {
    KeyOscMessage& msg = candidate.pressMessages[index];
    const String item = suffix + "_" + String(index);
    msg.address = server.arg("p_address" + item); msg.address.trim();
    msg.valueStr = server.arg("p_value" + item);
    msg.valueType = static_cast<ValueType>(constrain(server.arg("p_type" + item).toInt(), 0, 2));
    if (msg.valueStr.length() > 128) valid = false;
    if (msg.valueType == TYPE_INT) { int32_t parsed; valid = valid && parseInt32(msg.valueStr, parsed); }
    else if (msg.valueType == TYPE_FLOAT) { char* end = nullptr; const float parsed = strtof(msg.valueStr.c_str(), &end); valid = valid && end != msg.valueStr.c_str() && *end == '\0' && isfinite(parsed); }
  }
  for (uint8_t index = 0; valid && index < candidate.releaseMessageCount; ++index) {
    KeyOscMessage& msg = candidate.releaseMessages[index];
    const String item = suffix + "_" + String(index);
    msg.address = server.arg("r_address" + item); msg.address.trim();
    msg.valueStr = server.arg("r_value" + item);
    msg.valueType = static_cast<ValueType>(constrain(server.arg("r_type" + item).toInt(), 0, 2));
    if (msg.valueStr.length() > 128) valid = false;
    if (msg.valueType == TYPE_INT) { int32_t parsed; valid = valid && parseInt32(msg.valueStr, parsed); }
    else if (msg.valueType == TYPE_FLOAT) { char* end = nullptr; const float parsed = strtof(msg.valueStr.c_str(), &end); valid = valid && end != msg.valueStr.c_str() && *end == '\0' && isfinite(parsed); }
  }
  candidate.sequence.address = server.arg("seq_address" + suffix);
  candidate.sequence.address.trim();
  const String startText = server.arg("seq_start" + suffix);
  const String endText = server.arg("seq_end" + suffix);
  const String stepText = server.arg("seq_step" + suffix);
  char *startEnd = nullptr, *endEnd = nullptr, *stepEnd = nullptr;
  candidate.sequence.start = strtof(startText.c_str(), &startEnd);
  candidate.sequence.end = strtof(endText.c_str(), &endEnd);
  candidate.sequence.step = strtof(stepText.c_str(), &stepEnd);
  candidate.sequence.valueType = static_cast<ValueType>(constrain(server.arg("seq_type" + suffix).toInt(), 0, 2));
  valid = valid && startEnd != startText.c_str() && *startEnd == '\0' &&
          endEnd != endText.c_str() && *endEnd == '\0' &&
          stepEnd != stepText.c_str() && *stepEnd == '\0' &&
          isfinite(candidate.sequence.start) && isfinite(candidate.sequence.end) &&
          isfinite(candidate.sequence.step);
  keySettingsNormalizeSequence(candidate.sequence);
  return valid;
}

bool readEncoderSetting(size_t formIndex, EncoderSetting& candidate) {
  const String suffix = "_" + String(formIndex);
  const String identity = server.arg("identity" + suffix);
  EncoderSetting* current = nullptr;
  for (size_t index = 0; index < encoderSettingsCount(); ++index) {
    EncoderSetting* setting = encoderSettingsAt(index);
    if (setting && setting->identity == identity &&
        setting->connectedPortMask != 0) {
      current = setting;
      break;
    }
  }
  if (!current) return false;
  candidate = *current;
  candidate.displayName = server.arg("display_name" + suffix);
  candidate.displayName.trim();
  candidate.rotationAddress = server.arg("enc_rotation" + suffix);
  candidate.rotationAddress.trim();
  candidate.sendIncrement = server.arg("enc_increment" + suffix).toInt() != 0;

  auto readFloat = [&](const String& name, float& value) {
    const String text = server.arg(name + suffix);
    char* end = nullptr;
    value = strtof(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && isfinite(value);
  };
  String rotationError;
  bool valid = !candidate.displayName.isEmpty() &&
               candidate.displayName.length() <= 64 &&
               validJsonAddress(candidate.rotationAddress, rotationError);
  valid = valid && readFloat("enc_abs_min", candidate.absoluteInputMin) &&
          readFloat("enc_abs_max", candidate.absoluteInputMax) &&
          readFloat("enc_scale", candidate.incrementScale) &&
          readFloat("enc_out_min", candidate.outputMin) &&
          readFloat("enc_out_max", candidate.outputMax);
  candidate.outputType = static_cast<ValueType>(constrain(
      server.arg("enc_out_type" + suffix).toInt(), 0, 2));
  candidate.clickMode = server.arg("mode" + suffix).toInt() == MODE_SEQUENCE
                            ? MODE_SEQUENCE : MODE_PRESS_RELEASE;
  const int pressCount = server.arg("p_count" + suffix).toInt();
  const int releaseCount = server.arg("r_count" + suffix).toInt();
  if (pressCount < 0 || releaseCount < 0 ||
      pressCount + releaseCount > MAX_KEY_OSC_MESSAGES)
    return false;
  candidate.pressMessageCount = static_cast<uint8_t>(pressCount);
  candidate.releaseMessageCount = static_cast<uint8_t>(releaseCount);
  for (uint8_t index = 0; valid && index < candidate.pressMessageCount; ++index) {
    KeyOscMessage& message = candidate.pressMessages[index];
    const String item = suffix + "_" + String(index);
    message.address = server.arg("p_address" + item);
    message.address.trim();
    message.valueStr = server.arg("p_value" + item);
    message.valueType = static_cast<ValueType>(constrain(
        server.arg("p_type" + item).toInt(), 0, 2));
    String error;
    valid = validJsonAddress(message.address, error) &&
            message.valueStr.length() <= 128;
    if (valid && message.valueType == TYPE_INT) {
      int32_t parsed = 0;
      valid = parseInt32(message.valueStr, parsed);
    } else if (valid && message.valueType == TYPE_FLOAT) {
      char* end = nullptr;
      const float parsed = strtof(message.valueStr.c_str(), &end);
      valid = end != message.valueStr.c_str() && *end == '\0' && isfinite(parsed);
    }
  }
  for (uint8_t index = 0; valid && index < candidate.releaseMessageCount; ++index) {
    KeyOscMessage& message = candidate.releaseMessages[index];
    const String item = suffix + "_" + String(index);
    message.address = server.arg("r_address" + item);
    message.address.trim();
    message.valueStr = server.arg("r_value" + item);
    message.valueType = static_cast<ValueType>(constrain(
        server.arg("r_type" + item).toInt(), 0, 2));
    String error;
    valid = validJsonAddress(message.address, error) &&
            message.valueStr.length() <= 128;
    if (valid && message.valueType == TYPE_INT) {
      int32_t parsed = 0;
      valid = parseInt32(message.valueStr, parsed);
    } else if (valid && message.valueType == TYPE_FLOAT) {
      char* end = nullptr;
      const float parsed = strtof(message.valueStr.c_str(), &end);
      valid = end != message.valueStr.c_str() && *end == '\0' && isfinite(parsed);
    }
  }
  candidate.clickSequence.address = server.arg("seq_address" + suffix);
  candidate.clickSequence.address.trim();
  String addressError;
  valid = valid && validJsonAddress(candidate.clickSequence.address, addressError) &&
          readFloat("seq_start", candidate.clickSequence.start) &&
          readFloat("seq_end", candidate.clickSequence.end) &&
          readFloat("seq_step", candidate.clickSequence.step);
  candidate.clickSequence.valueType = static_cast<ValueType>(constrain(
      server.arg("seq_type" + suffix).toInt(), 0, 2));
  keySettingsNormalizeSequence(candidate.clickSequence);
  return valid;
}

bool readAngleSetting(size_t formIndex, AngleSetting& candidate) {
  const String suffix = "_" + String(formIndex);
  const String identity = server.arg("identity" + suffix);
  AngleSetting* current = nullptr;
  for (size_t index = 0; index < angleSettingsCount(); ++index) {
    AngleSetting* setting = angleSettingsAt(index);
    if (setting && setting->identity == identity &&
        setting->connectedPortMask != 0) {
      current = setting;
      break;
    }
  }
  if (!current) return false;
  candidate = *current;
  candidate.displayName = server.arg("display_name" + suffix);
  candidate.displayName.trim();
  candidate.address = server.arg("angle_address" + suffix);
  candidate.address.trim();
  candidate.use12Bit = server.arg("angle_12bit" + suffix).toInt() != 0;
  candidate.deadband = server.arg("angle_deadband" + suffix).toInt();
  auto readFloat = [&](const String& name, float& value) {
    const String text = server.arg(name + suffix);
    char* end = nullptr;
    value = strtof(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && isfinite(value);
  };
  String error;
  if (candidate.displayName.isEmpty() || candidate.displayName.length() > 64 ||
      !validJsonAddress(candidate.address, error) || candidate.deadband < 1 ||
      !readFloat("angle_out_min", candidate.outputMin) ||
      !readFloat("angle_out_max", candidate.outputMax))
    return false;
  candidate.outputType = static_cast<ValueType>(constrain(
      server.arg("angle_out_type" + suffix).toInt(), 0, 2));
  return true;
}

bool readTofSetting(size_t formIndex, TofSetting& candidate) {
  const String suffix = "_" + String(formIndex), identity = server.arg("identity" + suffix);
  TofSetting* current = nullptr;
  for (size_t index = 0; index < tofSettingsCount(); ++index) {
    TofSetting* setting = tofSettingsAt(index);
    if (setting && setting->identity == identity && setting->connectedPortMask) { current = setting; break; }
  }
  if (!current) return false;
  candidate = *current; candidate.displayName = server.arg("display_name" + suffix); candidate.displayName.trim();
  candidate.address = server.arg("tof_address" + suffix); candidate.address.trim();
  candidate.deadband = server.arg("tof_deadband" + suffix).toInt(); candidate.maxDistanceMm = server.arg("tof_max" + suffix).toInt();
  candidate.nearValueHigh = server.arg("tof_near_high" + suffix).toInt() != 0;
  auto readFloat = [&](const String& name, float& value) { const String text = server.arg(name + suffix); char* end = nullptr; value = strtof(text.c_str(), &end); return end != text.c_str() && *end == '\0' && isfinite(value); };
  String error; const int type = server.arg("tof_out_type" + suffix).toInt();
  if (candidate.displayName.isEmpty() || candidate.displayName.length() > 64 || !validJsonAddress(candidate.address, error) || candidate.deadband < 1 || candidate.deadband > 2000 || candidate.maxDistanceMm < 31 || candidate.maxDistanceMm > 2000 || !readFloat("tof_out_min", candidate.outputMin) || !readFloat("tof_out_max", candidate.outputMax) || type < TYPE_FLOAT || type > TYPE_INT) return false;
  candidate.outputType = static_cast<ValueType>(type); return true;
}

bool readJoystickSetting(size_t formIndex, JoystickSetting& candidate) {
  const String suffix="_"+String(formIndex),identity=server.arg("identity"+suffix);JoystickSetting* current=nullptr;for(size_t i=0;i<joystickSettingsCount();++i){JoystickSetting* s=joystickSettingsAt(i);if(s&&s->identity==identity&&s->connectedPortMask){current=s;break;}}if(!current)return false;candidate=*current;candidate.displayName=server.arg("display_name"+suffix);candidate.displayName.trim();candidate.xAddress=server.arg("joy_x"+suffix);candidate.yAddress=server.arg("joy_y"+suffix);candidate.xAddress.trim();candidate.yAddress.trim();candidate.deadband=server.arg("joy_deadband"+suffix).toInt();candidate.invertX=server.hasArg("joy_inv_x"+suffix);candidate.invertY=server.hasArg("joy_inv_y"+suffix);candidate.clickMode=server.arg("mode"+suffix).toInt()==MODE_SEQUENCE?MODE_SEQUENCE:MODE_PRESS_RELEASE;
  auto number=[&](const String& name,float& value){String text=server.arg(name+suffix);char* end=nullptr;value=strtof(text.c_str(),&end);return end!=text.c_str()&&*end=='\0'&&isfinite(value);};String error;if(candidate.displayName.isEmpty()||candidate.displayName.length()>64||!validJsonAddress(candidate.xAddress,error)||!validJsonAddress(candidate.yAddress,error)||candidate.deadband<1||candidate.deadband>254||!number("joy_out_min",candidate.outputMin)||!number("joy_out_max",candidate.outputMax))return false;candidate.outputType=(ValueType)constrain(server.arg("joy_out_type"+suffix).toInt(),0,2);
  int pc=server.arg("p_count"+suffix).toInt(),rc=server.arg("r_count"+suffix).toInt();if(pc<0||rc<0||pc+rc>MAX_KEY_OSC_MESSAGES)return false;candidate.pressMessageCount=pc;candidate.releaseMessageCount=rc;bool valid=true;auto readMessages=[&](bool press){uint8_t count=press?candidate.pressMessageCount:candidate.releaseMessageCount;KeyOscMessage* messages=press?candidate.pressMessages:candidate.releaseMessages;const String prefix=press?"p":"r";for(uint8_t i=0;valid&&i<count;++i){String item=suffix+"_"+String(i);messages[i].address=server.arg(prefix+"_address"+item);messages[i].address.trim();messages[i].valueStr=server.arg(prefix+"_value"+item);messages[i].valueType=(ValueType)constrain(server.arg(prefix+"_type"+item).toInt(),0,2);String e;valid=validJsonAddress(messages[i].address,e)&&messages[i].valueStr.length()<=128;if(valid&&messages[i].valueType==TYPE_INT){int32_t parsed;valid=parseInt32(messages[i].valueStr,parsed);}else if(valid&&messages[i].valueType==TYPE_FLOAT){char* end=nullptr;float v=strtof(messages[i].valueStr.c_str(),&end);valid=end!=messages[i].valueStr.c_str()&&*end=='\0'&&isfinite(v);}}};readMessages(true);readMessages(false);
  candidate.clickSequence.address=server.arg("seq_address"+suffix);candidate.clickSequence.address.trim();valid=valid&&validJsonAddress(candidate.clickSequence.address,error)&&number("seq_start",candidate.clickSequence.start)&&number("seq_end",candidate.clickSequence.end)&&number("seq_step",candidate.clickSequence.step);candidate.clickSequence.valueType=(ValueType)constrain(server.arg("seq_type"+suffix).toInt(),0,2);keySettingsNormalizeSequence(candidate.clickSequence);return valid;
}

void sendActionResult(int status, const String& message) {
  if (server.hasArg("ajax")) server.send(status, "text/plain; charset=utf-8", message);
  else sendStatusPage(message);
}

void handleSaveAll() {
  String host = server.arg("osc_host");
  String portText = server.arg("osc_port");
  host.trim(); portText.trim();
  bool numericPort = !portText.isEmpty();
  for (size_t i = 0; numericPort && i < portText.length(); ++i)
    numericPort = isdigit(static_cast<unsigned char>(portText[i]));
  const unsigned long port = numericPort ? portText.toInt() : 0;
  if (host.isEmpty() || host.length() > 253 || port < 1 || port > 65535) {
    sendActionResult(400, tr("Could not save settings. Check OSC Host and Port.", "設定を保存できませんでした。OSC送信先とポートを確認してください。"));
    return;
  }
  const int count = constrain(server.arg("connected_count").toInt(), 0, 40);
  for (int i = 0; i < count; ++i) {
    const int type = server.arg("device_type_" + String(i)).toInt();
    KeySetting keyCandidate;
    EncoderSetting encoderCandidate;
    AngleSetting angleCandidate;
    TofSetting tofCandidate;
    JoystickSetting joystickCandidate;
    if ((type == CHAIN_KEY_DEVICE_TYPE && !readKeySetting(i, keyCandidate)) ||
        (type == CHAIN_ENCODER_DEVICE_TYPE &&
         !readEncoderSetting(i, encoderCandidate)) ||
        (type == CHAIN_ANGLE_DEVICE_TYPE &&
         !readAngleSetting(i, angleCandidate)) ||
        (type == CHAIN_TOF_DEVICE_TYPE &&
         !readTofSetting(i, tofCandidate)) ||
        (type == CHAIN_JOYSTICK_DEVICE_TYPE && !readJoystickSetting(i,joystickCandidate)) ||
        (type != CHAIN_KEY_DEVICE_TYPE &&
         type != CHAIN_ENCODER_DEVICE_TYPE &&
         type != CHAIN_ANGLE_DEVICE_TYPE &&
         type != CHAIN_TOF_DEVICE_TYPE && type != CHAIN_JOYSTICK_DEVICE_TYPE)) {
      sendActionResult(400, tr("Could not save settings. Check the device fields.", "設定を保存できませんでした。デバイスの設定項目を確認してください。"));
      return;
    }
  }
  if (!oscSaveTarget(host, static_cast<uint16_t>(port))) {
    sendActionResult(500, tr("Could not write settings to storage.", "設定をストレージへ書き込めませんでした。"));
    return;
  }
  for (int i = 0; i < count; ++i) {
    const int type = server.arg("device_type_" + String(i)).toInt();
    bool saved = false;
    String savedIdentity;
    if (type == CHAIN_KEY_DEVICE_TYPE) {
      KeySetting candidate;
      saved = readKeySetting(i, candidate) && keySettingsSave(candidate);
      savedIdentity = candidate.identity;
    } else if (type == CHAIN_ENCODER_DEVICE_TYPE) {
      EncoderSetting candidate;
      saved = readEncoderSetting(i, candidate) && encoderSettingsSave(candidate);
      savedIdentity = candidate.identity;
    } else if (type == CHAIN_ANGLE_DEVICE_TYPE) {
      AngleSetting candidate;
      saved = readAngleSetting(i, candidate) && angleSettingsSave(candidate);
      savedIdentity = candidate.identity;
    } else if (type == CHAIN_TOF_DEVICE_TYPE) {
      TofSetting candidate;
      saved = readTofSetting(i, candidate) && tofSettingsSave(candidate);
      savedIdentity = candidate.identity;
    } else if(type==CHAIN_JOYSTICK_DEVICE_TYPE){JoystickSetting candidate;saved=readJoystickSetting(i,candidate)&&joystickSettingsSave(candidate);savedIdentity=candidate.identity;
    }
    NANO_STORAGE_LOGF("[ChainOSCnano][NVS] save_all index=%d count=%d type=%d uid=%s result=%s\n",
                      i, count, type, savedIdentity.c_str(), saved ? "ok" : "failed");
    if (!saved) {
      sendActionResult(500, tr("Could not write all device settings to storage.", "すべてのデバイス設定をストレージへ書き込めませんでした。"));
      return;
    }
  }
  sendActionResult(200, tr("All settings saved.", "すべての設定を保存しました。"));
}

void handleDeleteDevice() {
  const String identity = server.arg("identity");
  const int type = server.arg("device_type").toInt();
  const bool deleted = type == CHAIN_ENCODER_DEVICE_TYPE
                           ? encoderSettingsDelete(identity)
                       : type == CHAIN_ANGLE_DEVICE_TYPE
                           ? angleSettingsDelete(identity)
                       : type == CHAIN_TOF_DEVICE_TYPE
                           ? tofSettingsDelete(identity)
                       : type == CHAIN_JOYSTICK_DEVICE_TYPE
                           ? joystickSettingsDelete(identity)
                           : keySettingsDelete(identity);
  if (!deleted) {
    sendActionResult(400, tr("Could not delete device settings.", "デバイス設定を削除できませんでした。"));
    return;
  }
  sendActionResult(200, tr("Device settings deleted.", "デバイス設定を削除しました。"));
}

struct RequestedDevice {
  KeySetting* key = nullptr;
  EncoderSetting* encoder = nullptr;
  AngleSetting* angle = nullptr;
  TofSetting* tof = nullptr;
  JoystickSetting* joystick = nullptr;
};

RequestedDevice requestedConnectedDevice() {
  RequestedDevice result;
  if (!server.hasArg("index")) return result;
  const String text = server.arg("index");
  for (size_t index = 0; index < text.length(); ++index)
    if (!isdigit(static_cast<unsigned char>(text[index]))) return result;
  const int requested = text.toInt();
  int cardIndex = 0;

  // Built-in cards precede external Chain devices in handleRoot().
  for (size_t index = 0; index < keySettingsCount(); ++index) {
    KeySetting* setting = keySettingsAt(index);
    if (setting != nullptr && setting->builtIn) {
      if (cardIndex == requested) {
        result.key = setting;
        return result;
      }
      ++cardIndex;
    }
  }

  // Chain cards are rendered in physical-port order. Resolve the request by
  // that same topology so Identify and preset operations target the card the
  // user actually selected.
  if (requested < cardIndex) return result;
  const size_t physicalIndex = static_cast<size_t>(requested - cardIndex);
  String identity;
  uint8_t deviceType = 0;
  if (!chainProbeConnectedDeviceAt(physicalIndex, identity, deviceType)) {
    return result;
  }

  if (deviceType == 3) {  // Chain Key
    for (size_t index = 0; index < keySettingsCount(); ++index) {
      KeySetting* setting = keySettingsAt(index);
      if (setting && !setting->builtIn && setting->connectedPortMask != 0 &&
          setting->identity == identity) {
        result.key = setting;
        return result;
      }
    }
  } else if (deviceType == 1) {  // Chain Encoder
    for (size_t index = 0; index < encoderSettingsCount(); ++index) {
      EncoderSetting* setting = encoderSettingsAt(index);
      if (setting && setting->connectedPortMask != 0 &&
          setting->identity == identity) {
        result.encoder = setting;
        return result;
      }
    }
  } else if (deviceType == 2) {  // Chain Angle
    for (size_t index = 0; index < angleSettingsCount(); ++index) {
      AngleSetting* setting = angleSettingsAt(index);
      if (setting && setting->connectedPortMask != 0 &&
          setting->identity == identity) {
        result.angle = setting;
        return result;
      }
    }
  } else if (deviceType == 5) {  // Chain ToF
    for (size_t index = 0; index < tofSettingsCount(); ++index) {
      TofSetting* setting = tofSettingsAt(index);
      if (setting && setting->connectedPortMask != 0 &&
          setting->identity == identity) {
        result.tof = setting;
        return result;
      }
    }
  } else if (deviceType == 4) {  // Chain Joystick
    for (size_t index = 0; index < joystickSettingsCount(); ++index) {
      JoystickSetting* setting = joystickSettingsAt(index);
      if (setting && setting->connectedPortMask != 0 &&
          setting->identity == identity) {
        result.joystick = setting;
        return result;
      }
    }
  }
  return result;
}

void handleIdentifyDevice() {
  RequestedDevice selected = requestedConnectedDevice();
  String identity;
  if (selected.key) identity = selected.key->identity;
  else if (selected.encoder) identity = selected.encoder->identity;
  else if (selected.angle) identity = selected.angle->identity;
  else if (selected.tof) identity = selected.tof->identity;
  else if (selected.joystick) identity = selected.joystick->identity;
  if (identity.isEmpty()) {
    server.send(404, "text/plain; charset=utf-8",
                tr("The selected connected device was not found.",
                   "選択した接続済みデバイスが見つかりません。"));
    return;
  }
  const bool changed = identity == "nano:button"
                           ? nanoIdentifyDevice(identity)
                           : chainProbeIdentifyDevice(identity);
  if (!changed) {
    server.send(502, "text/plain; charset=utf-8",
                tr("The device LED could not be changed.",
                   "デバイスのLEDを変更できませんでした。"));
    return;
  }
  server.send(200, "text/plain; charset=utf-8",
              tr("Orange LED active for 10 seconds.",
                 "LEDを10秒間オレンジ色に点灯します。"));
}

void handleExportDevicePreset() {
  RequestedDevice selected = requestedConnectedDevice();
  if (!selected.key && !selected.encoder && !selected.angle && !selected.tof && !selected.joystick) {
    server.send(404, "text/plain; charset=utf-8",
                tr("The selected connected device was not found.", "選択した接続済みデバイスが見つかりません。"));
    return;
  }
  const bool encoder = selected.encoder != nullptr;
  const bool angle = selected.angle != nullptr;
  const bool tof = selected.tof != nullptr;
  const bool joystick = selected.joystick != nullptr;
  server.sendHeader("Content-Disposition", encoder
      ? "attachment; filename=\"ChainOSC-Encoder-preset.json\""
      : angle ? "attachment; filename=\"ChainOSC-Angle-preset.json\""
      : tof ? "attachment; filename=\"ChainOSC-ToF-preset.json\""
      : joystick ? "attachment; filename=\"ChainOSC-Joystick-preset.json\""
              : "attachment; filename=\"ChainOSC-Key-preset.json\"");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8",
              encoder ? encoderSettingJson(*selected.encoder, false)
              : angle ? angleSettingJson(*selected.angle, false)
              : tof ? tofSettingJson(*selected.tof, false)
              : joystick ? joystickSettingJson(*selected.joystick, false)
                      : keySettingJson(*selected.key, false));
}

void handleImportDevicePreset() {
  RequestedDevice selected = requestedConnectedDevice();
  if (!selected.key && !selected.encoder && !selected.angle && !selected.tof && !selected.joystick) {
    server.send(404, "text/plain; charset=utf-8",
                tr("The selected connected device was not found.", "選択した接続済みデバイスが見つかりません。"));
    return;
  }
  String body = server.arg("plain");
  if (body.isEmpty()) {
    server.send(400, "text/plain; charset=utf-8",
                tr("E_PRESET_FILE_EMPTY: The preset file is empty. Select a Device Preset JSON file that contains data.",
                   "E_PRESET_FILE_EMPTY: プリセットファイルが空です。内容を含むDevice Preset JSONファイルを選択してください。"));
    return;
  }
  if (body.length() > MAX_PRESET_BYTES) {
    server.send(413, "text/plain; charset=utf-8",
                tr("E_PRESET_FILE_TOO_LARGE: The preset file exceeds 16 KiB. Select a Device Preset JSON file no larger than 16 KiB.",
                   "E_PRESET_FILE_TOO_LARGE: プリセットファイルが16 KiBを超えています。16 KiB以内のDevice Preset JSONファイルを選択してください。"));
    return;
  }
  // Parse the mutable String buffer in zero-copy mode. Keep body alive until
  // every JsonVariant has been consumed because the document references it.
  DynamicJsonDocument document(16384);
  const DeserializationError parseError =
      deserializeJson(document, body.begin(), body.length());
  if (parseError) {
    server.send(400, "text/plain; charset=utf-8",
                String(tr("E_PRESET_JSON_MALFORMED: The JSON syntax is invalid. Check brackets, quotation marks, commas, and other JSON syntax.",
                          "E_PRESET_JSON_MALFORMED: JSONの構文が正しくありません。括弧、引用符、カンマなどを確認してください。")) +
                    " (" + parseError.c_str() + ")");
    return;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();
  const String format = root["format"].is<const char*>()
                            ? String(root["format"].as<const char*>()) : String();
  if (root.isNull() || (format != DEVICE_PRESET_FORMAT_NAME &&
                        format != LEGACY_DEVICE_PRESET_FORMAT_NAME)) {
    server.send(400, "text/plain; charset=utf-8",
                tr("E_PRESET_FORMAT_INVALID: This is not a supported ChainOSC Device Preset. Confirm that `format` is `ChainOSC-device-preset`.",
                   "E_PRESET_FORMAT_INVALID: 対応するChainOSC Device Presetではありません。`format`が`ChainOSC-device-preset`であることを確認してください。"));
    return;
  }
  if (!root["schemaVersion"].is<int>() ||
      root["schemaVersion"].as<int>() != DEVICE_PRESET_SCHEMA_VERSION) {
    server.send(400, "text/plain; charset=utf-8",
                tr("E_PRESET_SCHEMA_UNSUPPORTED: The preset `schemaVersion` is missing or unsupported. Use a preset exported by a compatible product version.",
                   "E_PRESET_SCHEMA_UNSUPPORTED: プリセットの`schemaVersion`がないか、対応していません。対応するバージョンの製品からエクスポートしたプリセットを使用してください。"));
    return;
  }
  const bool supportedType = root["deviceType"].is<int>() &&
      (root["deviceType"].as<int>() == CHAIN_KEY_DEVICE_TYPE ||
       root["deviceType"].as<int>() == CHAIN_ENCODER_DEVICE_TYPE ||
       root["deviceType"].as<int>() == CHAIN_ANGLE_DEVICE_TYPE ||
       root["deviceType"].as<int>() == CHAIN_JOYSTICK_DEVICE_TYPE ||
       root["deviceType"].as<int>() == CHAIN_TOF_DEVICE_TYPE);
  if (!supportedType) {
    server.send(400, "text/plain; charset=utf-8",
                tr("E_PRESET_DEVICE_TYPE_UNSUPPORTED: The preset device type is missing or unsupported. Use a preset for a supported ChainOSC device.",
                   "E_PRESET_DEVICE_TYPE_UNSUPPORTED: プリセットのデバイス種類がないか、対応していません。対応するChainOSCデバイスのプリセットを使用してください。"));
    return;
  }
  const int selectedType = selected.encoder ? CHAIN_ENCODER_DEVICE_TYPE
      : selected.angle ? CHAIN_ANGLE_DEVICE_TYPE
      : selected.tof ? CHAIN_TOF_DEVICE_TYPE
      : selected.joystick ? CHAIN_JOYSTICK_DEVICE_TYPE
      : CHAIN_KEY_DEVICE_TYPE;
  const int presetType = root["deviceType"].as<int>();
  if (presetType != selectedType) {
    server.send(400, "text/plain; charset=utf-8",
                tr("E_PRESET_DEVICE_TYPE_MISMATCH: The preset device type does not match the import target. Select a preset for the same device type.",
                   "E_PRESET_DEVICE_TYPE_MISMATCH: プリセットのデバイス種類がインポート先と一致しません。選択したデバイスと同じ種類のプリセットを使用してください。"));
    return;
  }
  const bool legacyPreset = format == LEGACY_DEVICE_PRESET_FORMAT_NAME;
  String error;
  if (!validateDevicePreset(root, presetType, legacyPreset, error)) {
    server.send(400, "text/plain; charset=utf-8",
                String(tr("Invalid preset: ", "プリセットが正しくありません: ")) + error);
    return;
  }
  if (legacyPreset) normalizeLegacyPresetTypes(document.as<JsonObject>(), presetType);
  bool saved = false;
  if (selected.encoder) {
    EncoderSetting candidate = *selected.encoder;
    if (!encoderSettingFromJson(root, candidate, false, error)) {
      server.send(400, "text/plain; charset=utf-8", String(tr("Invalid preset: ", "プリセットが正しくありません: ")) + error);
      return;
    }
    saved = encoderSettingsSave(candidate);
  } else if (selected.angle) {
    AngleSetting candidate = *selected.angle;
    if (!angleSettingFromJson(root, candidate, false, error)) {
      server.send(400, "text/plain; charset=utf-8", String(tr("Invalid preset: ", "プリセットが正しくありません: ")) + error);
      return;
    }
    saved = angleSettingsSave(candidate);
  } else if (selected.tof) {
    TofSetting candidate = *selected.tof;
    if (!tofSettingFromJson(root, candidate, false, error)) {
      server.send(400, "text/plain; charset=utf-8", String(tr("Invalid preset: ", "プリセットが正しくありません: ")) + error); return;
    }
    saved = tofSettingsSave(candidate);
  } else if(selected.joystick){JoystickSetting candidate=*selected.joystick;if(!joystickSettingFromJson(root,candidate,false,error)){server.send(400,"text/plain; charset=utf-8",String(tr("Invalid preset: ","プリセットが正しくありません: "))+error);return;}saved=joystickSettingsSave(candidate);
  } else {
    KeySetting candidate = *selected.key;
    if (!keySettingFromJson(root, candidate, false, error)) {
      server.send(400, "text/plain; charset=utf-8", String(tr("Invalid preset: ", "プリセットが正しくありません: ")) + error);
      return;
    }
    saved = keySettingsSave(candidate);
  }
  if (!saved) {
    server.send(507, "text/plain; charset=utf-8",
                tr("E_PRESET_STORAGE_WRITE_FAILED: The preset could not be written to storage. Existing settings were not changed. Check available storage and try again.",
                   "E_PRESET_STORAGE_WRITE_FAILED: プリセットをストレージへ書き込めませんでした。既存の設定は変更されていません。空き容量を確認してから再試行してください。"));
    return;
  }
  server.send(200, "text/plain; charset=utf-8", selected.encoder
      ? tr("Encoder preset imported.", "Encoderプリセットをインポートしました。")
      : selected.angle
          ? tr("Angle preset imported.", "Angleプリセットをインポートしました。")
      : selected.tof
          ? tr("ToF preset imported.", "ToFプリセットをインポートしました。")
      : selected.joystick
          ? tr("Joystick preset imported.", "Joystickプリセットをインポートしました。")
          : tr("Key preset imported.", "Keyプリセットをインポートしました。"));
}

void handleExportSettings() {
  server.sendHeader("Content-Disposition", "attachment; filename=\"ChainOSCnano-settings-v1.json\"");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json; charset=utf-8", "");
  String header = String("{\"format\":") + jsonString(SETTINGS_FORMAT_NAME) +
                  ",\"schemaVersion\":" + String(SETTINGS_SCHEMA_VERSION) +
                  ",\"firmwareVersion\":" + jsonString(APP_VERSION) +
                  ",\"wifiCredentialsIncluded\":false,\"global\":{\"oscHost\":" +
                  jsonString(oscTargetHost()) + ",\"oscPort\":" +
                  String(oscTargetPort()) + ",\"uiLanguage\":" +
                  jsonString(isJapaneseUi() ? "ja" : "en") + "},\"devices\":[";
  server.sendContent(header);
  bool firstDevice = true;
  for (size_t index = 0; index < keySettingsCount(); ++index) {
    KeySetting* setting = keySettingsAt(index);
    if (!setting) continue;
    String chunk = firstDevice ? "" : ",";
    chunk += keySettingJson(*setting, true);
    server.sendContent(chunk);
    firstDevice = false;
  }
  for(size_t index=0;index<joystickSettingsCount();++index){JoystickSetting* setting=joystickSettingsAt(index);if(!setting)continue;String chunk=firstDevice?"":",";chunk+=joystickSettingJson(*setting,true);server.sendContent(chunk);firstDevice=false;}
  for (size_t index = 0; index < tofSettingsCount(); ++index) {
    TofSetting* setting = tofSettingsAt(index); if (!setting) continue;
    String chunk = firstDevice ? "" : ","; chunk += tofSettingJson(*setting, true);
    server.sendContent(chunk); firstDevice = false;
  }
  for (size_t index = 0; index < angleSettingsCount(); ++index) {
    AngleSetting* setting = angleSettingsAt(index);
    if (!setting) continue;
    String chunk = firstDevice ? "" : ",";
    chunk += angleSettingJson(*setting, true);
    server.sendContent(chunk);
    firstDevice = false;
  }
  for (size_t index = 0; index < encoderSettingsCount(); ++index) {
    EncoderSetting* setting = encoderSettingsAt(index);
    if (!setting) continue;
    String chunk = firstDevice ? "" : ",";
    chunk += encoderSettingJson(*setting, true);
    server.sendContent(chunk);
    firstDevice = false;
  }
  server.sendContent("]}");
  server.sendContent("");
}

void handleImportSettings() {
  String body = server.arg("plain");
  if (body.isEmpty()) {
    server.send(400, "text/plain; charset=utf-8", tr("Import file is empty.", "インポートファイルが空です。"));
    return;
  }
  if (body.length() > MAX_IMPORT_BYTES) {
    server.send(413, "text/plain; charset=utf-8", tr("Import file exceeds 32 KiB.", "インポートファイルが32 KiBを超えています。"));
    return;
  }
  // Chain DualKey has no PSRAM. Parsing a const String duplicates every JSON
  // string and can exhaust internal Heap at the 32 KiB boundary. A mutable
  // input buffer lets ArduinoJson reference strings in-place instead.
  // Keep body alive until this handler has finished using the document.
  DynamicJsonDocument document(32768);
  const DeserializationError parseError =
      deserializeJson(document, body.begin(), body.length());
  if (parseError) {
    server.send(400, "text/plain; charset=utf-8", String(tr("Invalid JSON: ", "JSONが正しくありません: ")) + parseError.c_str());
    return;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();
  if (!root["format"].is<const char*>() ||
      String(root["format"].as<const char*>()) != SETTINGS_FORMAT_NAME) {
    server.send(400, "text/plain; charset=utf-8", tr("This is not a ChainOSCnano settings file.", "ChainOSCnanoの全体設定ファイルではありません。"));
    return;
  }
  if (!root["schemaVersion"].is<int>() ||
      root["schemaVersion"].as<int>() != SETTINGS_SCHEMA_VERSION) {
    server.send(400, "text/plain; charset=utf-8", tr("Unsupported or missing schemaVersion.", "schemaVersionがないか、対応していません。"));
    return;
  }
  JsonObjectConst global = root["global"].as<JsonObjectConst>();
  JsonArrayConst devices = root["devices"].as<JsonArrayConst>();
  if (global.isNull() || devices.isNull() || devices.size() > 40 ||
      !global["oscHost"].is<const char*>() || !global["oscPort"].is<int>()) {
    server.send(400, "text/plain; charset=utf-8", tr("Global settings or device list is invalid.", "共通設定またはデバイス一覧が正しくありません。"));
    return;
  }
  String host = global["oscHost"].as<const char*>();
  host.trim();
  const int port = global["oscPort"].as<int>();
  UiLanguage importedLanguage = uiLanguage;
  if (global["uiLanguage"].is<const char*>()) {
    const String language = global["uiLanguage"].as<const char*>();
    if (language != "en" && language != "ja") {
      server.send(400, "text/plain; charset=utf-8", tr("uiLanguage must be en or ja.", "uiLanguageはenまたはjaで指定してください。"));
      return;
    }
    importedLanguage = language == "ja" ? UiLanguage::JAPANESE : UiLanguage::ENGLISH;
  }
  if (host.isEmpty() || host.length() > 253 || port < 1 || port > 65535) {
    server.send(400, "text/plain; charset=utf-8", tr("OSC target is out of range.", "OSC送信先の設定が範囲外です。"));
    return;
  }

  for (size_t index = 0; index < devices.size(); ++index) {
    String error;
    JsonObjectConst object = devices[index].as<JsonObjectConst>();
    const int type = object["deviceType"] | -1;
    String identity;
    bool validDevice = false;
    if (type == CHAIN_KEY_DEVICE_TYPE) {
      KeySetting candidate;
      validDevice = keySettingFromJson(object, candidate, true, error);
      identity = candidate.identity;
    } else if (type == CHAIN_ENCODER_DEVICE_TYPE) {
      EncoderSetting candidate;
      validDevice = encoderSettingFromJson(object, candidate, true, error);
      identity = candidate.identity;
    } else if (type == CHAIN_ANGLE_DEVICE_TYPE) {
      AngleSetting candidate;
      validDevice = angleSettingFromJson(object, candidate, true, error);
      identity = candidate.identity;
    } else if (type == CHAIN_TOF_DEVICE_TYPE) {
      TofSetting candidate; validDevice = tofSettingFromJson(object, candidate, true, error); identity = candidate.identity;
    } else if(type==CHAIN_JOYSTICK_DEVICE_TYPE){JoystickSetting candidate;validDevice=joystickSettingFromJson(object,candidate,true,error);identity=candidate.identity;
    } else {
      error = tr("Unsupported device type.", "対応していないデバイス種類です。");
    }
    if (!validDevice) {
      server.send(400, "text/plain; charset=utf-8", String(tr("Device ", "デバイス ")) + String(index + 1) + ": " + error);
      return;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      JsonObjectConst previousObject = devices[previous].as<JsonObjectConst>();
      if (previousObject["identity"].is<const char*>() &&
          String(previousObject["identity"].as<const char*>()) == identity) {
        server.send(400, "text/plain; charset=utf-8", String(tr("Duplicate device identity: ", "デバイス識別子が重複しています: ")) + identity);
        return;
      }
    }
  }

  if (!oscSaveTarget(host, static_cast<uint16_t>(port))) {
    server.send(507, "text/plain; charset=utf-8", tr("Global settings could not be written to storage.", "共通設定をストレージへ書き込めませんでした。"));
    return;
  }
  for (size_t index = 0; index < devices.size(); ++index) {
    String error;
    JsonObjectConst object = devices[index].as<JsonObjectConst>();
    const int type = object["deviceType"] | -1;
    bool saved = false;
    if (type == CHAIN_KEY_DEVICE_TYPE) {
      KeySetting candidate;
      if (!keySettingFromJson(object, candidate, true, error)) {
        server.send(400, "text/plain; charset=utf-8", error); return;
      }
      KeySetting* destination = keySettingsEnsure(
          candidate.identity, candidate.displayName,
          candidate.sequence.address.length() ? candidate.sequence.address
                                               : "/chainoscnano/imported");
      if (destination) {
        candidate.connectedPortMask = destination->connectedPortMask;
        candidate.builtIn = destination->builtIn;
        saved = keySettingsSave(candidate);
      }
    } else if (type == CHAIN_ENCODER_DEVICE_TYPE) {
      EncoderSetting candidate;
      if (!encoderSettingFromJson(object, candidate, true, error)) {
        server.send(400, "text/plain; charset=utf-8", error); return;
      }
      EncoderSetting* destination =
          encoderSettingsEnsure(candidate.identity, candidate.displayName);
      if (destination) {
        candidate.connectedPortMask = destination->connectedPortMask;
        saved = encoderSettingsSave(candidate);
      }
    } else if (type == CHAIN_ANGLE_DEVICE_TYPE) {
      AngleSetting candidate;
      if (!angleSettingFromJson(object, candidate, true, error)) {
        server.send(400, "text/plain; charset=utf-8", error); return;
      }
      AngleSetting* destination =
          angleSettingsEnsure(candidate.identity, candidate.displayName);
      if (destination) {
        candidate.connectedPortMask = destination->connectedPortMask;
        saved = angleSettingsSave(candidate);
      }
    } else if (type == CHAIN_TOF_DEVICE_TYPE) {
      TofSetting candidate;
      if (!tofSettingFromJson(object, candidate, true, error)) { server.send(400, "text/plain; charset=utf-8", error); return; }
      TofSetting* destination = tofSettingsEnsure(candidate.identity, candidate.displayName);
      if (destination) { candidate.connectedPortMask = destination->connectedPortMask; saved = tofSettingsSave(candidate); }
    } else if(type==CHAIN_JOYSTICK_DEVICE_TYPE){JoystickSetting candidate;if(!joystickSettingFromJson(object,candidate,true,error)){server.send(400,"text/plain; charset=utf-8",error);return;}JoystickSetting* destination=joystickSettingsEnsure(candidate.identity,candidate.displayName);if(destination){candidate.connectedPortMask=destination->connectedPortMask;saved=joystickSettingsSave(candidate);}
    }
    if (!saved) {
      server.send(507, "text/plain; charset=utf-8", String(tr("Storage write failed at device ", "デバイス設定の書き込みに失敗しました: ")) + String(index + 1));
      return;
    }
  }
  uiLanguage = importedLanguage;
  saveUiLanguage();
  server.send(200, "text/plain; charset=utf-8", String(tr("Import completed. ", "インポートが完了しました。")) + String(devices.size()) + tr(" device(s) restored.", "件のデバイス設定を復元しました。"));
}

void handleSaveKey() {
  const String identity = server.arg("identity");
  KeySetting* current = nullptr;
  for (size_t index = 0; index < keySettingsCount(); ++index) {
    KeySetting* setting = keySettingsAt(index);
    if (setting != nullptr && setting->identity == identity) {
      current = setting;
      break;
    }
  }
  if (current == nullptr) {
    sendStatusPage(tr("Unknown key setting.", "対象のキー設定が見つかりません。"));
    return;
  }

  KeySetting candidate = *current;
  candidate.displayName = server.arg("display_name");
  candidate.displayName.trim();
  candidate.mode = server.arg("mode").toInt() == MODE_SEQUENCE
                       ? MODE_SEQUENCE : MODE_PRESS_RELEASE;
  const int pressCount = server.arg("p_count").toInt();
  const int releaseCount = server.arg("r_count").toInt();
  if (pressCount < 0 || releaseCount < 0 ||
      pressCount + releaseCount > MAX_KEY_OSC_MESSAGES) {
    sendStatusPage(tr("Press and Release messages must total 8 or fewer.", "PressとReleaseのメッセージは合計8件以内にしてください。"));
    return;
  }
  candidate.pressMessageCount = pressCount;
  candidate.releaseMessageCount = releaseCount;

  bool valid = !candidate.displayName.isEmpty();
  for (uint8_t index = 0; valid && index < candidate.pressMessageCount; ++index) {
    KeyOscMessage& message = candidate.pressMessages[index];
    message.address = server.arg("p_address_" + String(index));
    message.address.trim();
    message.valueStr = server.arg("p_value_" + String(index));
    message.valueType = static_cast<ValueType>(constrain(
        server.arg("p_type_" + String(index)).toInt(),
        static_cast<int>(TYPE_FLOAT), static_cast<int>(TYPE_STRING)));
    if (message.valueStr.length() > 128) valid = false;
    if (message.valueType == TYPE_INT) {
      int32_t parsed;
      valid = valid && parseInt32(message.valueStr, parsed);
    } else if (message.valueType == TYPE_FLOAT) {
      char* end = nullptr;
      const float parsed = strtof(message.valueStr.c_str(), &end);
      valid = valid && end != message.valueStr.c_str() && *end == '\0' && isfinite(parsed);
    }
  }
  for (uint8_t index = 0; valid && index < candidate.releaseMessageCount; ++index) {
    KeyOscMessage& message = candidate.releaseMessages[index];
    message.address = server.arg("r_address_" + String(index));
    message.address.trim();
    message.valueStr = server.arg("r_value_" + String(index));
    message.valueType = static_cast<ValueType>(constrain(
        server.arg("r_type_" + String(index)).toInt(),
        static_cast<int>(TYPE_FLOAT), static_cast<int>(TYPE_STRING)));
    if (message.valueStr.length() > 128) valid = false;
    if (message.valueType == TYPE_INT) {
      int32_t parsed;
      valid = valid && parseInt32(message.valueStr, parsed);
    } else if (message.valueType == TYPE_FLOAT) {
      char* end = nullptr;
      const float parsed = strtof(message.valueStr.c_str(), &end);
      valid = valid && end != message.valueStr.c_str() && *end == '\0' && isfinite(parsed);
    }
  }
  candidate.sequence.address = server.arg("seq_address");
  candidate.sequence.address.trim();
  char* startEnd = nullptr;
  char* endEnd = nullptr;
  char* stepEnd = nullptr;
  const String startText = server.arg("seq_start");
  const String endText = server.arg("seq_end");
  const String stepText = server.arg("seq_step");
  candidate.sequence.start = strtof(startText.c_str(), &startEnd);
  candidate.sequence.end = strtof(endText.c_str(), &endEnd);
  candidate.sequence.step = strtof(stepText.c_str(), &stepEnd);
  candidate.sequence.valueType = static_cast<ValueType>(constrain(
      server.arg("seq_type").toInt(), static_cast<int>(TYPE_FLOAT),
      static_cast<int>(TYPE_STRING)));
  valid = valid && startEnd != startText.c_str() && *startEnd == '\0' &&
          endEnd != endText.c_str() && *endEnd == '\0' &&
          stepEnd != stepText.c_str() && *stepEnd == '\0' &&
          isfinite(candidate.sequence.start) && isfinite(candidate.sequence.end) &&
          isfinite(candidate.sequence.step);
  keySettingsNormalizeSequence(candidate.sequence);

  if (!valid || !keySettingsSave(candidate)) {
    sendStatusPage(tr("Could not save key settings. Check Address and values.", "キー設定を保存できませんでした。Addressと値を確認してください。"));
    return;
  }
  sendStatusPage(tr("Key settings saved.", "キー設定を保存しました。"));
}

void handleSaveOsc() {
  String host = server.arg("osc_host");
  String portText = server.arg("osc_port");
  host.trim();
  portText.trim();
  bool numericPort = !portText.isEmpty();
  for (size_t index = 0; numericPort && index < portText.length(); ++index) {
    numericPort = isdigit(static_cast<unsigned char>(portText[index]));
  }
  const unsigned long portValue = numericPort ? portText.toInt() : 0;
  if (host.isEmpty() || host.length() > 253 || portValue < 1 ||
      portValue > 65535 ||
      !oscSaveTarget(host, static_cast<uint16_t>(portValue))) {
    sendStatusPage(tr("Could not save OSC target. Check Host and Port.", "OSC送信先を保存できませんでした。ホストとポートを確認してください。"));
    return;
  }
  sendStatusPage(tr("OSC target saved.", "OSC送信先を保存しました。"));
}

void handleRoot() {
  applyBrowserLanguageOnFirstVisit();
  if (networkState == NetworkState::AP_MODE) {
    sendProvisioningPage();
  } else {
    sendStatusPage();
  }
}

void handleSetLanguage() {
  if (server.hasArg("language")) {
    uiLanguage = server.arg("language") == "ja" ? UiLanguage::JAPANESE
                                                  : UiLanguage::ENGLISH;
    saveUiLanguage();
  }
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void scheduleRestart() {
  restartScheduled = true;
  restartAtMs = millis() + NETWORK_RESTART_DELAY_MS;
}

bool validWifiInput(const String& ssid, const String& password,
                    String& error) {
  const size_t ssidBytes = ssid.length();
  const size_t passwordBytes = password.length();
  if (ssidBytes == 0 || ssidBytes > 32) {
    error = tr("SSID must be 1–32 bytes.", "SSIDは1～32バイトで入力してください。");
    return false;
  }
  bool valid64DigitPsk = passwordBytes == 64;
  for (size_t index = 0; valid64DigitPsk && index < passwordBytes; ++index) {
    valid64DigitPsk = isxdigit(static_cast<unsigned char>(password[index]));
  }
  if (passwordBytes != 0 &&
      (passwordBytes < 8 || (passwordBytes > 63 && !valid64DigitPsk))) {
    error = tr("Password must be blank, 8–63 bytes, or a 64-digit hexadecimal PSK.", "パスワードは空欄、8～63バイト、または64桁の16進数PSKで入力してください。");
    return false;
  }
  return true;
}

void handleSaveWifi() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String error;
  if (!validWifiInput(ssid, password, error)) {
    sendProvisioningPage(error);
    return;
  }

  if (!systemSettingsSaveWifi(ssid, password)) {
    sendProvisioningPage(tr("Could not save Wi-Fi settings.", "Wi-Fi設定を保存できませんでした。"));
    return;
  }

  NANO_VERBOSE_LOGF("[ChainOSCnano][NET] credentials_saved ssid_bytes=%u password_bytes=%u\n",
                static_cast<unsigned int>(ssid.length()),
                static_cast<unsigned int>(password.length()));
  String html = pageStart("Wi-Fi Saved");
  html += F("<h1>ChainOSCnano Settings</h1><div class='card'><h2>"); html += tr("Wi-Fi settings saved", "Wi-Fi設定を保存しました");
  html += F("</h2><p class='status'>"); html += tr("Restarting ChainOSCnano…", "ChainOSCnanoを再起動します…"); html += F("</p></div>");
  sendPage(html);
  scheduleRestart();
}

void handleForgetWifi() {
  const bool cleared = systemSettingsClearWifi();
  NANO_VERBOSE_LOGF("[ChainOSCnano][NET] credentials_cleared=%s\n",
                cleared ? "true" : "false");
  String html = pageStart("Wi-Fi Settings Deleted");
  html += F("<h1>ChainOSCnano Settings</h1><div class='card'><h2>"); html += tr("Wi-Fi settings deleted", "Wi-Fi設定を削除しました");
  html += F("</h2><p class='status'>"); html += tr("Restarting in setup mode…", "設定モードで再起動します…"); html += F("</p></div>");
  sendPage(html);
  scheduleRestart();
}

void registerRoutes() {
  if (routesRegistered) {
    return;
  }
  const char* trackedHeaders[] = {"Accept-Language"};
  server.collectHeaders(trackedHeaders, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/set_language", HTTP_POST, handleSetLanguage);
  server.on("/save-wifi", HTTP_POST, handleSaveWifi);
  server.on("/forget-wifi", HTTP_POST, handleForgetWifi);
  server.on("/save-osc", HTTP_POST, handleSaveOsc);
  server.on("/save-key", HTTP_POST, handleSaveKey);
  server.on("/save-all", HTTP_POST, handleSaveAll);
  server.on("/delete_device", HTTP_POST, handleDeleteDevice);
  server.on("/export_settings", HTTP_GET, handleExportSettings);
  server.on("/import_settings", HTTP_POST, handleImportSettings);
  server.on("/export_device_preset", HTTP_GET, handleExportDevicePreset);
  server.on("/import_device_preset", HTTP_POST, handleImportDevicePreset);
  server.on("/identify_device", HTTP_POST, handleIdentifyDevice);
  server.on("/generate_204", HTTP_ANY, handleRoot);
  server.on("/hotspot-detect.html", HTTP_ANY, handleRoot);
  server.on("/ncsi.txt", HTTP_ANY, handleRoot);
  server.on("/connecttest.txt", HTTP_ANY, handleRoot);
  server.on("/fwlink", HTTP_ANY, handleRoot);
  server.on("/redirect", HTTP_ANY, handleRoot);
  server.on("/canonical.html", HTTP_ANY, handleRoot);
  server.on("/success.txt", HTTP_ANY, handleRoot);
  server.onNotFound(handleRoot);
  routesRegistered = true;
}

void startAccessPoint(const char* reason) {
  if (mdnsRunning) {
    MDNS.end();
    mdnsRunning = false;
  }
  WiFi.mode(WIFI_AP);
  const IPAddress apIp(192, 168, 4, 1);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  const bool started = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  const esp_err_t txPowerResult =
      esp_wifi_set_max_tx_power(WIFI_TX_POWER_QDBM);
  delay(500);
  dnsServer.start(CAPTIVE_DNS_PORT, "*", apIp);
  registerRoutes();
  if (!webServerStarted) {
    server.begin();
    webServerStarted = true;
  }
  networkState = NetworkState::AP_MODE;
  nanoSetNetworkLedState(NetworkLedState::AP_MODE);
  NANO_VERBOSE_LOGF(
      "[ChainOSCnano][NET] state=AP_MODE reason=%s started=%s ssid=%s "
      "ip=%s tx_power_qdbm=%d tx_power_result=%d\n",
      reason, started ? "true" : "false", WIFI_AP_SSID,
      apIp.toString().c_str(), static_cast<int>(WIFI_TX_POWER_QDBM),
      static_cast<int>(txPowerResult));
}

void startStationConnection() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
  const esp_err_t txPowerResult =
      esp_wifi_set_max_tx_power(WIFI_TX_POWER_QDBM);
  networkState = NetworkState::CONNECTING;
  nanoSetNetworkLedState(NetworkLedState::CONNECTING);
  NANO_VERBOSE_LOGF("[ChainOSCnano][NET] state=CONNECTING ssid_bytes=%u timeout_ms=%lu tx_power_qdbm=%d tx_power_result=%d\n",
                static_cast<unsigned int>(savedSsid.length()),
                WIFI_CONNECT_TIMEOUT_MS,
                static_cast<int>(WIFI_TX_POWER_QDBM),
                static_cast<int>(txPowerResult));
}

void handleConnected() {
  networkState = NetworkState::CONNECTED;
  nanoSetNetworkLedState(NetworkLedState::CONNECTED);
  const bool mdnsStarted = MDNS.begin(WIFI_MDNS_HOST);
  mdnsRunning = mdnsStarted;
  if (mdnsStarted) {
    MDNS.addService("http", "tcp", 80);
  }
  registerRoutes();
  if (!webServerStarted) {
    server.begin();
    webServerStarted = true;
  }
  NANO_VERBOSE_LOGF(
      "[ChainOSCnano][NET] state=CONNECTED ip=%s mdns=%s.local "
      "mdns_started=%s rssi=%d\n",
      WiFi.localIP().toString().c_str(), WIFI_MDNS_HOST,
      mdnsStarted ? "true" : "false", WiFi.RSSI());
}

}  // namespace

void networkSetup() {
  NANO_VERBOSE_PRINTLN("[ChainOSCnano][NET] setup_begin=true");

  savedSsid = systemSettingsWifiSsid();
  savedPassword = systemSettingsWifiPassword();
  uiLanguageConfigured = systemSettingsHasUiLanguage();
  if (uiLanguageConfigured)
    uiLanguage = static_cast<UiLanguage>(systemSettingsUiLanguage());

  if (savedSsid.length() == 0) {
    startAccessPoint("no_saved_credentials");
  } else {
    startStationConnection();
    const unsigned long startedAtMs = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startedAtMs < WIFI_CONNECT_TIMEOUT_MS) {
      delay(300);
    }
    if (WiFi.status() == WL_CONNECTED) {
      handleConnected();
    } else {
      startAccessPoint("connect_timeout");
    }
  }

  NANO_VERBOSE_LOGF("[ChainOSCnano][NET] web_server_started=%s\n",
                webServerStarted ? "true" : "false");
}

void networkUpdate() {
  if (webServerStarted) {
    server.handleClient();
  }

  if (networkState == NetworkState::AP_MODE) {
    dnsServer.processNextRequest();
  } else if (networkState == NetworkState::CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) handleConnected();
  } else if (networkState == NetworkState::CONNECTED &&
             WiFi.status() != WL_CONNECTED) {
    if (mdnsRunning) {
      MDNS.end();
      mdnsRunning = false;
    }
    WiFi.reconnect();
    networkState = NetworkState::CONNECTING;
    nanoSetNetworkLedState(NetworkLedState::CONNECTING);
    NANO_VERBOSE_PRINTLN("[ChainOSCnano][NET] state=RECONNECTING");
  }

  if (restartScheduled &&
      static_cast<long>(millis() - restartAtMs) >= 0) {
    NANO_VERBOSE_PRINTLN("[ChainOSCnano][NET] restarting=true");
    delay(20);
    ESP.restart();
  }
}
