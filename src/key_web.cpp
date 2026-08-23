#include "key_web.h"

#include <math.h>
#include <new>

#include "chain_probe.h"
#include "key_settings.h"

namespace {

const char* tr(bool ja, const char* en, const char* jp) { return ja ? jp : en; }

String esc(const String& value) {
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': out += F("&amp;"); break; case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break; case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break; default: out += value[i];
    }
  }
  return out;
}

String typeOptions(ValueType selected, bool ja) {
  String html;
  const char* names[] = {"Float", "Int", "String"};
  for (uint8_t type = 0; type < 3; ++type) {
    html += F("<option value='"); html += type; html += F("'");
    if (selected == type) html += F(" selected");
    html += F(">"); html += names[type]; html += F("</option>");
  }
  return html;
}

String messageRow(const KeyOscMessage& message, const char* event,
                  uint8_t card, uint8_t order, bool ja) {
  const String prefix = String(event) + "_" + String(card) + "_" + String(order);
  String html = F("<div class='msg-row'><div class='move'><button type='button' onclick='moveMsg(this,-1)'>↑</button><button type='button' onclick='moveMsg(this,1)'>↓</button></div><div><label>");
  html += tr(ja, "OSC Address", "OSCアドレス");
  html += F("</label><input data-field='address' maxlength='192' required name='"); html += prefix;
  html += F("_address' value='"); html += esc(message.address); html += F("'></div><div><label>");
  html += tr(ja, "Type", "型"); html += F("</label><select data-field='type' name='");
  html += prefix; html += F("_type'>"); html += typeOptions(message.type, ja); html += F("</select></div><div><label>");
  html += tr(ja, "Value", "値"); html += F("</label><input data-field='value' maxlength='128' name='");
  html += prefix; html += F("_value' value='"); html += esc(message.value); html += F("'></div><button class='delete-msg' type='button' onclick='deleteMsg(this)'>");
  html += tr(ja, "Delete", "削除"); html += F("</button></div>");
  return html;
}

String sequenceHtml(const KeySetting& setting, uint8_t card, bool ja) {
  const String c(card);
  String html = F("<div class='sequence' data-sequence style='display:");
  html += setting.mode == MODE_SEQUENCE ? F("block") : F("none");
  html += F("'><h3>");
  html += tr(ja, "Sequence", "シーケンス"); html += F("</h3><div class='seq-grid'><div class='wide'><label>");
  html += tr(ja, "OSC Address", "OSCアドレス"); html += F("</label><input maxlength='192' required name='seq_address_");
  html += c; html += F("' value='"); html += esc(setting.sequence.address); html += F("'></div><div><label>");
  html += tr(ja, "Start", "開始値"); html += F("</label><input type='number' step='any' required name='seq_start_"); html += c; html += F("' value='"); html += String(setting.sequence.start, 6);
  html += F("'></div><div><label>"); html += tr(ja, "End", "終了値"); html += F("</label><input type='number' step='any' required name='seq_end_"); html += c; html += F("' value='"); html += String(setting.sequence.end, 6);
  html += F("'></div><div><label>"); html += tr(ja, "Step", "増減量"); html += F("</label><input type='number' step='any' required name='seq_step_"); html += c; html += F("' value='"); html += String(setting.sequence.step, 6);
  html += F("'></div><div><label>"); html += tr(ja, "Type", "型"); html += F("</label><select name='seq_type_"); html += c; html += F("'>"); html += typeOptions(setting.sequence.type, ja); html += F("</select></div></div></div>");
  return html;
}

bool parseFloatStrict(const String& text, float& value) {
  char* end = nullptr; value = strtof(text.c_str(), &end);
  return end != text.c_str() && *end == '\0' && isfinite(value);
}

bool readMessage(WebServer& server, const String& prefix,
                 KeyOscMessage& message) {
  message.address = server.arg(prefix + "_address"); message.address.trim();
  message.value = server.arg(prefix + "_value");
  const int type = server.arg(prefix + "_type").toInt();
  if (type < 0 || type > 2) return false;
  message.type = static_cast<ValueType>(type);
  return keySettingsValidMessage(message);
}

}  // namespace

String keyWebStyles() {
  return F(".device{border-left:5px solid #7545d6}.device-head{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.badge{background:#eeeafe;padding:4px 9px;border-radius:12px;font-weight:bold}.uid{font-family:monospace;background:#f0f0f0;padding:7px;margin:10px 0;word-break:break-all}.key-grid,.seq-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.wide{grid-column:1/-1}.mode-box{margin:12px 0}.messages{margin-top:12px}.event{background:#f7f9fc;padding:10px;border-radius:8px;margin-top:10px}.msg-row{display:grid;grid-template-columns:auto minmax(180px,3fr) minmax(90px,1fr) minmax(120px,1.5fr) auto;gap:8px;align-items:end;background:#fff;border:1px solid #dae1ec;border-radius:8px;padding:10px;margin-top:8px}.move{display:flex;gap:3px}.move button,.delete-msg{width:auto;margin:0;padding:10px;background:#fff;color:#3267e3;border:1px solid #cbd5e1}.delete-msg{color:#dc3545}.add{background:#fff;color:#3267e3;border:1px dashed #8ba9ed}.add:disabled{color:#98a2b3;background:#eee;border-color:#ccc}.save-all{background:#20a849;position:sticky;bottom:10px}.dirty-status{color:#b45f06;font-weight:bold;white-space:nowrap}.saved{border-left:5px solid #ff9800}.toast{position:fixed;z-index:30;left:50%;bottom:78px;transform:translateX(-50%);padding:11px 18px;border-radius:8px;background:#17324d;color:#fff;box-shadow:0 4px 16px rgba(0,0,0,.25)}@media(max-width:620px){.key-grid,.seq-grid{grid-template-columns:1fr}.wide{grid-column:auto}.msg-row{grid-template-columns:auto 1fr}.msg-row>div:not(.move),.delete-msg{grid-column:2}.move{grid-row:1/5;flex-direction:column}.save-bar{flex-wrap:wrap}.dirty-status{width:100%}}" );
}

#if 0
String keyWebScript() {
  return F("<script>function cardOf(e){return e.closest('[data-card]')}function rows(c){return c.querySelectorAll('.msg-row')}function refresh(c){let i=+c.dataset.card,p=0,r=0;rows(c).forEach(x=>{let ev=x.closest('[data-event]').dataset.event,n=ev==='p'?p++:r++;x.querySelectorAll('[data-field]').forEach(f=>f.name=ev+'_'+i+'_'+n+'_'+f.dataset.field)});c.querySelector('[data-pcount]').value=p;c.querySelector('[data-rcount]').value=r;c.querySelector('[data-total]').textContent=(p+r)+' / 8';c.querySelectorAll('.add').forEach(b=>b.disabled=p+r>=8)}function moveMsg(b,d){let x=b.closest('.msg-row'),g=x.parentNode,y=d<0?x.previousElementSibling:x.nextElementSibling;if(y){d<0?g.insertBefore(x,y):g.insertBefore(y,x)}refresh(cardOf(b))}function deleteMsg(b){let c=cardOf(b);b.closest('.msg-row').remove();refresh(c)}function addMsg(b){let c=cardOf(b);if(rows(c).length>=8)return;let ev=b.dataset.add,g=c.querySelector('[data-event="'+ev+'"] [data-rows]'),x=document.createElement('div');x.className='msg-row';x.innerHTML='<div class="move"><button type="button" onclick="moveMsg(this,-1)">↑</button><button type="button" onclick="moveMsg(this,1)">↓</button></div><div><label>'+c.dataset.osc+'</label><input data-field="address" maxlength="192" required value="'+c.dataset.address+'"></div><div><label>'+c.dataset.type+'</label><select data-field="type"><option value="0">Float</option><option value="1" selected>Int</option><option value="2">String</option></select></div><div><label>'+c.dataset.value+'</label><input data-field="value" maxlength="128" value="'+(ev==='p'?'1':'0')+'"></div><button class="delete-msg" type="button" onclick="deleteMsg(this)">'+c.dataset.del+'</button>';g.appendChild(x);refresh(c)}function modeChanged(s){let c=cardOf(s),q=s.value==='1';c.querySelector('[data-pr]').style.display=q?'none':'block';c.querySelector('[data-sequence]').style.display=q?'block':'none'}document.addEventListener('DOMContentLoaded',()=>document.querySelectorAll('[data-card]').forEach(refresh));</script>");
}

#endif

String keyWebScript() {
  return String(R"JS(<script>
function cardOf(e){return e.closest('[data-card]')}
function rows(c){return c.querySelectorAll('.msg-row')}
function markDirty(){let status=document.getElementById('dirty-status');if(status)status.hidden=false}
function showToast(message){let toast=document.getElementById('save-toast');if(!toast)return;toast.textContent=message;toast.hidden=false;clearTimeout(window.toastTimer);window.toastTimer=setTimeout(()=>toast.hidden=true,3000)}
async function saveSettings(event){
  event.preventDefault();let form=event.currentTarget,button=form.querySelector('.save-all');
  if(!form.reportValidity())return;button.disabled=true;
  try{let response=await fetch('/save-all?ajax=1',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});let message=await response.text();if(!response.ok)throw new Error(message);let status=document.getElementById('dirty-status');if(status)status.hidden=true;showToast(message)}
  catch(error){alert(error.message||'Could not save settings.')}finally{button.disabled=false}
}
async function deleteSavedKey(event,form){
  event.preventDefault();if(!confirm(form.dataset.confirm))return;
  try{let response=await fetch('/delete-key?ajax=1',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});let message=await response.text();if(!response.ok)throw new Error(message);let card=form.closest('.saved');if(card)card.remove();showToast(message)}
  catch(error){alert(error.message||'Could not delete device settings.')}
}
function refresh(c){
  let i=+c.dataset.card,p=0,r=0;
  rows(c).forEach(x=>{
    let ev=x.closest('[data-event]').dataset.event,n=ev==='p'?p++:r++;
    x.querySelectorAll('[data-field]').forEach(f=>f.name=ev+'_'+i+'_'+n+'_'+f.dataset.field)
  });
  c.querySelector('[data-pcount]').value=p;
  c.querySelector('[data-rcount]').value=r;
  c.querySelector('[data-total]').textContent=(p+r)+' / 8';
  c.querySelectorAll('.add').forEach(b=>b.disabled=p+r>=8)
}
function moveMsg(b,d){
  let x=b.closest('.msg-row'),g=x.parentNode,y=d<0?x.previousElementSibling:x.nextElementSibling;
  if(y){d<0?g.insertBefore(x,y):g.insertBefore(y,x)}
  refresh(cardOf(b));markDirty()
}
function deleteMsg(b){let c=cardOf(b);b.closest('.msg-row').remove();refresh(c);markDirty()}
function addMsg(b){
  let c=cardOf(b);if(rows(c).length>=8)return;
  let ev=b.dataset.add,g=c.querySelector('[data-event="'+ev+'"] [data-rows]'),x=document.createElement('div');
  x.className='msg-row';
  x.innerHTML='<div class="move"><button type="button" onclick="moveMsg(this,-1)">↑</button><button type="button" onclick="moveMsg(this,1)">↓</button></div><div><label>'+c.dataset.osc+'</label><input data-field="address" maxlength="192" required value="'+c.dataset.address+'"></div><div><label>'+c.dataset.type+'</label><select data-field="type"><option value="0">Float</option><option value="1" selected>Int</option><option value="2">String</option></select></div><div><label>'+c.dataset.value+'</label><input data-field="value" maxlength="128" value="'+(ev==='p'?'1':'0')+'"></div><button class="delete-msg" type="button" onclick="deleteMsg(this)">'+c.dataset.del+'</button>';
  g.appendChild(x);refresh(c);markDirty()
}
function modeChanged(s){
  let c=cardOf(s),q=s.value==='1';
  c.querySelector('[data-pr]').style.display=q?'none':'block';
  c.querySelector('[data-sequence]').style.display=q?'block':'none';markDirty()
}
function showEvent(button,index){
  let card=cardOf(button),panels=card.querySelectorAll('.event-panel');
  panels.forEach((panel,i)=>panel.style.display=i===index?'block':'none');
  button.parentNode.querySelectorAll('.event-tab').forEach((tab,i)=>tab.classList.toggle('active',i===index))
}
document.addEventListener('DOMContentLoaded',()=>{
  document.querySelectorAll('[data-card]').forEach(card=>{
    refresh(card);
    let panels=card.querySelectorAll('.event');
    if(panels.length<2)return;
    panels.forEach(panel=>panel.classList.add('event-panel'));
    panels[1].style.display='none';
    let tabs=document.createElement('div');tabs.className='event-tabs';
    panels.forEach((panel,index)=>{
      let button=document.createElement('button');button.type='button';
      button.className='event-tab'+(index===0?' active':'');
      button.textContent=panel.querySelector('h3').textContent;
      button.addEventListener('click',()=>showEvent(button,index));tabs.appendChild(button)
    });
    panels[0].before(tabs)
  });
  let form=document.getElementById('settings-form');
  if(form){form.addEventListener('input',markDirty);form.addEventListener('change',markDirty);form.addEventListener('submit',saveSettings)}
});
</script>)JS");
}

String keyWebConnectedHtml(bool ja) {
  const size_t connectedCount = chainProbeKeyCount();
  String html = F("<h2>"); html += tr(ja, "Connected devices", "接続中のデバイス"); html += F("</h2>");
  if (connectedCount == 0) { html += F("<div class='card'><p class='note'>"); html += tr(ja, "No Chain Keys connected.", "接続中のChain Keyはありません。"); html += F("</p></div>"); }
  KeySetting* active[CHAIN_MAX_DEVICES] = {};
  size_t count = 0;
  for (size_t index = 0; index < connectedCount; ++index) {
    KeySetting* setting = keySettingsEnsure(chainProbeKeyUid(index));
    if (setting) active[count++] = setting;
  }
  if (count < connectedCount) {
    html += F("<div class='card'><p style='color:#c73c4a'>");
    html += tr(ja, "The saved device limit has been reached. Delete an unused saved device setting.", "保存済みデバイス数が上限に達しています。不要な保存済みデバイス設定を削除してください。");
    html += F("</p></div>");
  }
  html += F("<input type='hidden' name='key_count' value='"); html += count; html += F("'>");
  for (size_t card = 0; card < count; ++card) {
    KeySetting* setting = active[card]; const String uid = setting->uid;
    const String c(card); const String defaultAddr = setting->pressCount ? setting->press[0].address : setting->sequence.address;
    html += F("<div class='card device' data-card='"); html += c; html += F("' data-address='"); html += esc(defaultAddr); html += F("' data-osc='"); html += tr(ja,"OSC Address","OSCアドレス"); html += F("' data-type='"); html += tr(ja,"Type","型"); html += F("' data-value='"); html += tr(ja,"Value","値"); html += F("' data-del='"); html += tr(ja,"Delete","削除"); html += F("'><div class='device-head'><span class='badge'>Key</span><strong>"); html += esc(setting->name); html += F("</strong><span class='status'>"); html += tr(ja, "Connected", "接続済み"); html += F("</span></div><div class='uid'>"); html += uid; html += F("</div><input type='hidden' name='uid_"); html += c; html += F("' value='"); html += uid; html += F("'><div class='key-grid'><div><label>"); html += tr(ja, "Device Name", "デバイス名"); html += F("</label><input maxlength='64' name='name_"); html += c; html += F("' value='"); html += esc(setting->name); html += F("'></div><div><label>"); html += tr(ja, "Key Mode", "キーモード"); html += F("</label><select name='mode_"); html += c; html += F("' onchange='modeChanged(this)'><option value='0'"); if (setting->mode == MODE_PRESS_RELEASE) html += F(" selected"); html += F(">"); html += tr(ja, "Press / Release", "押した時／離した時"); html += F("</option><option value='1'"); if (setting->mode == MODE_SEQUENCE) html += F(" selected"); html += F(">"); html += tr(ja, "Sequence", "シーケンス"); html += F("</option></select></div></div><div data-pr style='display:"); html += setting->mode == MODE_SEQUENCE ? F("none") : F("block"); html += F("'><div class='status'>"); html += tr(ja, "Messages ", "メッセージ "); html += F("<span data-total></span></div><input data-pcount type='hidden' name='p_count_"); html += c; html += F("'><input data-rcount type='hidden' name='r_count_"); html += c; html += F("'><div class='event' data-event='p'><h3>"); html += tr(ja, "When Pressed", "押した時"); html += F("</h3><div data-rows>"); for (uint8_t i=0;i<setting->pressCount;++i) html += messageRow(setting->press[i],"p",card,i,ja); html += F("</div><button class='add' data-add='p' type='button' onclick='addMsg(this)'>+ "); html += tr(ja,"Add OSC Message","OSCメッセージを追加"); html += F("</button></div><div class='event' data-event='r'><h3>"); html += tr(ja,"When Released","離した時"); html += F("</h3><div data-rows>"); for(uint8_t i=0;i<setting->releaseCount;++i) html += messageRow(setting->release[i],"r",card,i,ja); html += F("</div><button class='add' data-add='r' type='button' onclick='addMsg(this)'>+ "); html += tr(ja,"Add OSC Message","OSCメッセージを追加"); html += F("</button></div></div>"); html += sequenceHtml(*setting,card,ja); html += F("</div>");
  }
  html += F("<div class='save-bar'><span id='dirty-status' class='dirty-status' hidden>"); html += tr(ja,"Unsaved changes","未保存の変更があります"); html += F("</span><button class='save-all' type='submit'>"); html += tr(ja,"Save All Settings","すべての設定を保存"); html += F("</button></div></form>");
  return html;
}

String keyWebSavedHtml(bool ja) {
  String html = F("<div class='card'><h2>");
  html += tr(ja, "Saved Device Settings", "保存済みデバイス設定");
  html += F("</h2><p class='note'>");
  html += tr(ja, "Only devices whose settings have been saved are shown.",
             "設定を保存したデバイスのみ表示します。");
  html += F("</p></div>");
  bool any = false;
  for (size_t i = 0; i < keySettingsCount(); ++i) {
    KeySetting* setting = keySettingsAt(i);
    if (!setting || !setting->persisted) continue;
    const bool connected = chainProbeKeyConnected(setting->uid);
    any = true;
    html += F("<div class='card saved'><div class='device-head'><span class='badge'>Key</span><strong>");
    html += esc(setting->name);
    html += connected ? F("</strong><span class='badge badge-on'>")
                      : F("</strong><span class='badge badge-off'>");
    html += connected ? tr(ja, "Connected", "接続済み")
                      : tr(ja, "Disconnected", "未接続");
    html += F("</span></div><div class='uid'>");
    html += setting->uid;
    html += F("</div>");
    if (!connected) {
      html += F("<form method='POST' action='/delete-key' data-confirm='");
      html += tr(ja, "Delete settings for this device?", "このデバイスの設定を削除しますか？");
      html += F("' onsubmit='deleteSavedKey(event,this)'><input type='hidden' name='uid' value='");
      html += setting->uid;
      html += F("'><button class='danger' type='submit'>");
      html += tr(ja, "Delete Settings", "設定を削除");
      html += F("</button></form>");
    }
    html += F("</div>");
  }
  if (!any) {
    html += F("<div class='card'><p class='note'>");
    html += tr(ja, "No saved device settings.",
               "保存済みデバイス設定はありません。");
    html += F("</p></div>");
  }
  return html;
}

bool keyWebSave(WebServer& server, String& error) {
  const int count=server.arg("key_count").toInt();if(count<0||count>CHAIN_MAX_DEVICES){error="Invalid device count.";return false;}KeySetting* candidates=new(std::nothrow) KeySetting[count];if(count&& !candidates){error="Not enough memory.";return false;}
  for(int i=0;i<count;++i){const String suffix=String(i),uid=server.arg("uid_"+suffix);KeySetting* current=keySettingsFind(uid);if(!current){error="Unknown device.";delete[] candidates;return false;}KeySetting& c=candidates[i];c=*current;c.name=server.arg("name_"+suffix);c.name.trim();const int mode=server.arg("mode_"+suffix).toInt();if(c.name.length()>DEVICE_NAME_MAX_BYTES||mode<0||mode>1){error="Invalid device name or mode.";delete[] candidates;return false;}c.mode=static_cast<KeyMode>(mode);const int pc=server.arg("p_count_"+suffix).toInt(),rc=server.arg("r_count_"+suffix).toInt();if(pc<0||rc<0||pc+rc>MAX_KEY_OSC_MESSAGES){error="OSC message limit exceeded.";delete[] candidates;return false;}c.pressCount=pc;c.releaseCount=rc;for(int m=0;m<pc;++m)if(!readMessage(server,"p_"+suffix+"_"+String(m),c.press[m])){error="Invalid Press message.";delete[] candidates;return false;}for(int m=0;m<rc;++m)if(!readMessage(server,"r_"+suffix+"_"+String(m),c.release[m])){error="Invalid Release message.";delete[] candidates;return false;}c.sequence.address=server.arg("seq_address_"+suffix);c.sequence.address.trim();const int st=server.arg("seq_type_"+suffix).toInt();if(st<0||st>2||!keySettingsValidAddress(c.sequence.address)||!parseFloatStrict(server.arg("seq_start_"+suffix),c.sequence.start)||!parseFloatStrict(server.arg("seq_end_"+suffix),c.sequence.end)||!parseFloatStrict(server.arg("seq_step_"+suffix),c.sequence.step)){error="Invalid Sequence settings.";delete[] candidates;return false;}c.sequence.type=static_cast<ValueType>(st);keySettingsNormalizeSequence(c.sequence);}
  bool ok=true;for(int i=0;i<count&&ok;++i){KeySetting* current=keySettingsFind(candidates[i].uid);*current=candidates[i];ok=keySettingsSave(*current);}delete[] candidates;if(!ok)error="Could not write device settings.";return ok;
}

bool keyWebDelete(WebServer& server, String& error) {const String uid=server.arg("uid");if(uid.isEmpty()||chainProbeKeyConnected(uid)){error="Connected device settings cannot be deleted.";return false;}if(!keySettingsDelete(uid)){error="Could not delete device settings.";return false;}return true;}
