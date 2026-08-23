#include "network_manager.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>

#include "config.h"
#include "nano_hardware.h"
#include "osc_manager.h"
#include "key_web.h"

namespace {

enum class NetworkState : uint8_t {
  CONNECTING,
  CONNECTED,
  AP_MODE,
};

WebServer server(80);
DNSServer dnsServer;
NetworkState networkState = NetworkState::CONNECTING;
String savedSsid;
String savedPassword;
bool mdnsRunning = false;
bool routesRegistered = false;
bool webServerStarted = false;
bool restartScheduled = false;
unsigned long restartAtMs = 0;
unsigned long lastConnectingLedChangeMs = 0;
bool connectingLedOn = true;
bool japaneseUi = false;
bool uiLanguageConfigured = false;

void updateConnectingLed(bool force = false) {
  const unsigned long now = millis();
  if (!force && now - lastConnectingLedChangeMs < WIFI_CONNECT_LED_BLINK_MS) {
    return;
  }
  if (!force) connectingLedOn = !connectingLedOn;
  lastConnectingLedChangeMs = now;
  nanoHardwareSetColor(0, 0, connectingLedOn ? 255 : 0);
}

bool isJapaneseRequest() {
  return japaneseUi;
}

void saveUiLanguage() {
  Preferences preferences;
  if (preferences.begin("ui", false)) {
    preferences.putUChar("language", japaneseUi ? 1 : 0);
    preferences.end();
    uiLanguageConfigured = true;
  }
}

void applyBrowserLanguageOnFirstVisit() {
  if (uiLanguageConfigured) return;
  String accepted = server.header("Accept-Language");
  accepted.toLowerCase();
  if (!accepted.isEmpty()) {
    japaneseUi = accepted.startsWith("ja");
    saveUiLanguage();
  }
}

const char* tr(bool japanese, const char* english, const char* japaneseText) {
  return japanese ? japaneseText : english;
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

String pageStart(const char* title, bool japanese) {
  String html;
  html.reserve(1800);
  html += F("<!doctype html><html lang='");
  html += japanese ? F("ja") : F("en");
  html += F("'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>");
  html += title;
  html += F("</title><style>body{font-family:sans-serif;margin:16px;background:#f5f5f5;color:#18212f}main{max-width:1080px;margin:auto}.card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px;box-shadow:0 2px 5px rgba(0,0,0,.1)}h1{font-size:1.45em}h2{font-size:1.1em}h3{font-size:1em}label{display:block;margin-top:8px;font-weight:bold}input,select{width:100%;padding:10px;margin-top:5px;box-sizing:border-box;font-size:1em}button{width:100%;padding:12px;margin-top:16px;border:0;border-radius:6px;background:#3267e3;color:#fff;font-size:1em}.danger{background:#dc3545}.status{padding:11px;background:#edf3ff;color:#244da7;border-radius:8px}.note{color:#667085;line-height:1.5;word-break:break-word}");
  html += keyWebStyles();
  html += F(".badge-on{background:#d4edda;color:#155724}.badge-off{background:#f8d7da;color:#721c24}.event-tabs{display:flex;gap:4px;padding:4px;background:#edf0f4;border-radius:9px;margin-top:12px}.event-tab{margin:0;background:transparent;color:#697586}.event-tab.active{background:#fff;color:#18212f;box-shadow:0 1px 4px #bbb}.event-panel{margin-top:12px;background:transparent;padding:0;border-radius:0}.event-panel>h3{display:none}.save-bar{position:sticky;z-index:15;bottom:8px;display:flex;align-items:center;gap:12px;padding:10px 12px;margin:16px 0 28px;background:rgba(255,255,255,.96);border:1px solid #dce2ea;border-radius:10px;box-shadow:0 5px 18px rgba(0,0,0,.14)}.save-bar .save-all{position:static;flex:1;margin:0;background:#28a745}.language-row{display:flex;align-items:center;justify-content:space-between;gap:12px}.language-row h2{margin:0}.language-row form{margin:0;min-width:150px}.language-row select{margin:0}.system-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.system-item{padding:10px;background:#f8f9fa;border-radius:6px}.system-item strong{display:block;margin-bottom:5px;font-size:.9em}.system-item code{word-break:break-all}@media(max-width:620px){.system-grid{grid-template-columns:1fr}}");
  html += F("</style></head><body><main>");
  return html;
}

void sendPage(String html) {
  html += F("</main></body></html>");
  server.send(200, "text/html; charset=utf-8", html);
}

void sendProvisioningPage(const String& error = "") {
  const bool japanese = isJapaneseRequest();
  String html = pageStart("ChainOSCnano Wi-Fi Setup", japanese);
  html += F("<h1>ChainOSCnano</h1><div class='card language-row'><h2>");
  html += tr(japanese, "Language", "言語");
  html += F("</h2><form action='/set_language' method='POST'><select name='language' onchange='this.form.submit()'><option value='en'");
  if (!japanese) html += F(" selected");
  html += F(">English</option><option value='ja'");
  if (japanese) html += F(" selected");
  html += F(">日本語</option></select></form></div><div class='card'><h2>");
  html += tr(japanese, "Wi-Fi setup", "Wi-Fi設定");
  html += F("</h2><p class='status'>");
  html += tr(japanese,
             "Connect this device to a 2.4 GHz Wi-Fi network.",
             "このデバイスを2.4 GHz帯のWi-Fiへ接続します。");
  html += F("</p>");
  if (!error.isEmpty()) {
    html += F("<p style='color:#c73c4a'>");
    html += htmlEscape(error);
    html += F("</p>");
  }
  html += F("<form method='POST' action='/save-wifi'><label>SSID</label><input name='ssid' maxlength='32' required><label>");
  html += tr(japanese, "Password", "パスワード");
  html += F("</label><input name='password' type='password' maxlength='64'><button type='submit'>");
  html += tr(japanese, "Save and restart", "保存して再起動");
  html += F("</button></form><p class='note'>");
  html += tr(japanese,
             "The password may be blank for an open network, 8–63 characters, or a 64-digit hexadecimal PSK.",
             "オープンネットワークでは空欄、通常は8～63文字、または64桁の16進数PSKを入力してください。");
  html += F("</p></div>");
  sendPage(html);
}

void sendConnectedPage(const String& message = "", bool error = false) {
  const bool japanese = isJapaneseRequest();
  String html = pageStart("ChainOSCnano", japanese);
  html += F("<div id='save-toast' class='toast' hidden></div><h1>Chain OSC Setting</h1><div class='card language-row'><h2>");
  html += tr(japanese, "Language", "言語");
  html += F("</h2><form action='/set_language' method='POST'><select name='language' onchange='this.form.submit()'><option value='en'");
  if (!japanese) html += F(" selected");
  html += F(">English</option><option value='ja'");
  if (japanese) html += F(" selected");
  html += F(">日本語</option></select></form></div><div class='card'><h2>");
  html += tr(japanese, "System", "システム");
  html += F("</h2><p class='status'>");
  html += tr(japanese, "Wi-Fi connected", "Wi-Fi接続済み");
  html += F("</p><div class='system-grid'><div class='system-item'><strong>");
  html += tr(japanese, "Product", "製品名");
  html += F("</strong><code>ChainOSCnano</code></div><div class='system-item'><strong>Version</strong>");
  html += APP_VERSION;
  html += F("</div><div class='system-item'><strong>");
  html += tr(japanese, "IP Address", "IPアドレス");
  html += F("</strong><code>");
  html += WiFi.localIP().toString();
  html += F("</code></div><div class='system-item'><strong>mDNS</strong><code>http://");
  html += WIFI_MDNS_HOST;
  html += F(".local/</code></div></div></div><div class='card'><h2>WiFi</h2><p class='note'>IP: ");
  html += WiFi.localIP().toString();
  html += F("</p><form method='POST' action='/forget-wifi'><button class='danger' type='submit'>");
  html += tr(japanese, "Delete Wi-Fi settings", "Wi-Fi設定を削除");
  html += F("</button></form></div>");
  if (!message.isEmpty()) {
    html += error ? F("<p style='color:#c73c4a'>") : F("<p class='status'>");
    html += htmlEscape(message);
    html += F("</p>");
  }
  html += F("<form id='settings-form' method='POST' action='/save-all'><div class='card'><h2>");
  html += tr(japanese, "OSC destination", "OSC送信先");
  html += F("</h2><label>");
  html += tr(japanese, "IPv4 address", "IPv4アドレス");
  html += F("</label><input name='osc_host' inputmode='decimal' maxlength='15' required value='");
  html += htmlEscape(oscTargetHost());
  html += F("'><label>");
  html += tr(japanese, "UDP port", "UDPポート");
  html += F("</label><input name='osc_port' type='number' min='1' max='65535' required value='");
  html += String(oscTargetPort());
  html += F("'></div>");
  html += keyWebConnectedHtml(japanese);
  html += keyWebSavedHtml(japanese);
  html += keyWebScript();
  sendPage(html);
}

void handleRoot() {
  applyBrowserLanguageOnFirstVisit();
  if (networkState == NetworkState::AP_MODE) sendProvisioningPage();
  else sendConnectedPage();
}

bool parseOscFromRequest(String& host, uint16_t& port, String& error) {
  const bool japanese = isJapaneseRequest();
  host = server.arg("osc_host");
  host.trim();
  const String portText = server.arg("osc_port");
  IPAddress parsedAddress;
  bool portDigitsOnly = !portText.isEmpty();
  for (size_t index = 0; portDigitsOnly && index < portText.length(); ++index) {
    portDigitsOnly = isdigit(static_cast<unsigned char>(portText[index]));
  }
  const unsigned long parsedPort = portText.toInt();
  if (!parsedAddress.fromString(host)) {
    error = tr(japanese, "Enter a valid IPv4 address.", "正しいIPv4アドレスを入力してください。");
    return false;
  }
  if (!portDigitsOnly || parsedPort < 1 || parsedPort > 65535) {
    error = tr(japanese, "UDP port must be between 1 and 65535.", "UDPポートは1～65535で入力してください。");
    return false;
  }
  port = static_cast<uint16_t>(parsedPort);
  return true;
}

void handleSaveAll() {
  String saveError;
  String oscHost;
  uint16_t oscPort = 0;
  if (!parseOscFromRequest(oscHost, oscPort, saveError)) {
    if (server.hasArg("ajax")) server.send(400, "text/plain; charset=utf-8", saveError);
    else sendConnectedPage(saveError, true);
    return;
  }
  if (!keyWebSave(server, saveError)) {
    if (server.hasArg("ajax")) server.send(400, "text/plain; charset=utf-8", saveError);
    else sendConnectedPage(saveError, true);
    return;
  }
  if (!oscSaveTarget(oscHost, oscPort)) {
    const String error = tr(isJapaneseRequest(),
                            "Could not save the OSC destination.",
                            "OSC送信先を保存できませんでした。");
    if (server.hasArg("ajax")) server.send(500, "text/plain; charset=utf-8", error);
    else sendConnectedPage(error, true);
    return;
  }
  const bool japanese = isJapaneseRequest();
  const String message = tr(japanese, "All settings saved.",
                            "すべての設定を保存しました。");
  if (server.hasArg("ajax")) server.send(200, "text/plain; charset=utf-8", message);
  else sendConnectedPage(message);
}

void handleSetLanguage() {
  if (server.hasArg("language")) {
    japaneseUi = server.arg("language") == "ja";
    saveUiLanguage();
  }
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleDeleteKey() {
  String deleteError;
  if (!keyWebDelete(server, deleteError)) {
    if (server.hasArg("ajax")) server.send(400, "text/plain; charset=utf-8", deleteError);
    else sendConnectedPage(deleteError, true);
    return;
  }
  const bool japanese = isJapaneseRequest();
  const String message = tr(japanese, "Saved device settings deleted.",
                            "保存済みデバイス設定を削除しました。");
  if (server.hasArg("ajax")) server.send(200, "text/plain; charset=utf-8", message);
  else sendConnectedPage(message);
}

bool validWifiInput(const String& ssid, const String& password,
                    String& error) {
  const bool japanese = isJapaneseRequest();
  if (ssid.length() == 0 || ssid.length() > 32) {
    error = tr(japanese, "SSID must be 1–32 bytes.",
               "SSIDは1～32バイトで入力してください。");
    return false;
  }
  bool valid64DigitPsk = password.length() == 64;
  for (size_t index = 0; valid64DigitPsk && index < password.length(); ++index) {
    valid64DigitPsk = isxdigit(static_cast<unsigned char>(password[index]));
  }
  if (password.length() != 0 &&
      (password.length() < 8 ||
       (password.length() > 63 && !valid64DigitPsk))) {
    error = tr(japanese,
               "Password must be blank, 8–63 bytes, or a 64-digit hexadecimal PSK.",
               "パスワードは空欄、8～63バイト、または64桁の16進数PSKで入力してください。");
    return false;
  }
  return true;
}

void scheduleRestart() {
  restartScheduled = true;
  restartAtMs = millis() + NETWORK_RESTART_DELAY_MS;
}

void handleSaveWifi() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String error;
  if (!validWifiInput(ssid, password, error)) {
    sendProvisioningPage(error);
    return;
  }

  Preferences preferences;
  if (!preferences.begin(WIFI_PREFS_NAMESPACE, false)) {
    sendProvisioningPage("Could not open settings storage.");
    return;
  }
  const size_t ssidWritten = preferences.putString("ssid", ssid);
  const size_t passwordWritten = preferences.putString("password", password);
  preferences.end();
  if (ssidWritten == 0 || (password.length() > 0 && passwordWritten == 0)) {
    sendProvisioningPage("Could not save Wi-Fi settings.");
    return;
  }

  Serial.printf(
      "[ChainOSCnano][NET] credentials_saved ssid_bytes=%u password_bytes=%u\n",
      static_cast<unsigned int>(ssid.length()),
      static_cast<unsigned int>(password.length()));
  const bool japanese = isJapaneseRequest();
  String html = pageStart("Wi-Fi Saved", japanese);
  html += F("<h1>ChainOSCnano</h1><div class='card'><p class='status'>");
  html += tr(japanese, "Wi-Fi settings saved. Restarting…",
             "Wi-Fi設定を保存しました。再起動します…");
  html += F("</p></div>");
  sendPage(html);
  scheduleRestart();
}

void handleForgetWifi() {
  Preferences preferences;
  bool cleared = false;
  if (preferences.begin(WIFI_PREFS_NAMESPACE, false)) {
    const bool ssidRemoved =
        !preferences.isKey("ssid") || preferences.remove("ssid");
    const bool passwordRemoved =
        !preferences.isKey("password") || preferences.remove("password");
    cleared = ssidRemoved && passwordRemoved;
    preferences.end();
  }
  Serial.printf("[ChainOSCnano][NET] credentials_cleared=%s\n",
                cleared ? "true" : "false");
  const bool japanese = isJapaneseRequest();
  String html = pageStart("Wi-Fi Deleted", japanese);
  html += F("<h1>ChainOSCnano</h1><div class='card'><p class='status'>");
  html += tr(japanese, "Wi-Fi settings deleted. Restarting in setup mode…",
             "Wi-Fi設定を削除しました。設定モードで再起動します…");
  html += F("</p></div>");
  sendPage(html);
  scheduleRestart();
}

void registerRoutes() {
  if (routesRegistered) return;
  const char* trackedHeaders[] = {"Accept-Language"};
  server.collectHeaders(trackedHeaders, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/set_language", HTTP_POST, handleSetLanguage);
  server.on("/save-wifi", HTTP_POST, handleSaveWifi);
  server.on("/forget-wifi", HTTP_POST, handleForgetWifi);
  server.on("/save-all", HTTP_POST, handleSaveAll);
  server.on("/delete-key", HTTP_POST, handleDeleteKey);
  server.on("/generate_204", HTTP_ANY, handleRoot);
  server.on("/hotspot-detect.html", HTTP_ANY, handleRoot);
  server.on("/ncsi.txt", HTTP_ANY, handleRoot);
  server.on("/connecttest.txt", HTTP_ANY, handleRoot);
  server.on("/canonical.html", HTTP_ANY, handleRoot);
  server.on("/success.txt", HTTP_ANY, handleRoot);
  server.on("/fwlink", HTTP_ANY, handleRoot);
  server.on("/redirect", HTTP_ANY, handleRoot);
  server.onNotFound(handleRoot);
  routesRegistered = true;
}

void startWebServer() {
  registerRoutes();
  if (!webServerStarted) {
    server.begin();
    webServerStarted = true;
  }
}

void startAccessPoint(const char* reason) {
  if (mdnsRunning) {
    MDNS.end();
    mdnsRunning = false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  const IPAddress apIp(192, 168, 4, 1);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  const bool started = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  delay(250);
  dnsServer.start(CAPTIVE_DNS_PORT, "*", apIp);
  startWebServer();
  networkState = NetworkState::AP_MODE;
  nanoHardwareSetColor(255, 0, 0);
  Serial.printf(
      "[ChainOSCnano][NET] state=AP_MODE reason=%s started=%s ssid=%s ip=%s\n",
      reason, started ? "true" : "false", WIFI_AP_SSID,
      apIp.toString().c_str());
}

void startStationConnection() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
  networkState = NetworkState::CONNECTING;
  connectingLedOn = true;
  updateConnectingLed(true);
  Serial.printf(
      "[ChainOSCnano][NET] state=CONNECTING ssid_bytes=%u timeout_ms=%lu\n",
      static_cast<unsigned int>(savedSsid.length()),
      WIFI_CONNECT_TIMEOUT_MS);
}

void handleConnected() {
  networkState = NetworkState::CONNECTED;
  nanoHardwareSetColor(0, 255, 255);
  const bool mdnsStarted = MDNS.begin(WIFI_MDNS_HOST);
  mdnsRunning = mdnsStarted;
  if (mdnsStarted) MDNS.addService("http", "tcp", 80);
  startWebServer();
  Serial.printf(
      "[ChainOSCnano][NET] state=CONNECTED ip=%s mdns=%s.local "
      "mdns_started=%s rssi=%d\n",
      WiFi.localIP().toString().c_str(), WIFI_MDNS_HOST,
      mdnsStarted ? "true" : "false", WiFi.RSSI());
}

}  // namespace

void networkSetup() {
  Serial.println("[ChainOSCnano][NET] setup_begin=true");
  Preferences preferences;
  if (preferences.begin("ui", true)) {
    if (preferences.isKey("language")) {
      japaneseUi = preferences.getUChar("language", 0) != 0;
      uiLanguageConfigured = true;
    }
    preferences.end();
  }
  if (preferences.begin(WIFI_PREFS_NAMESPACE, true)) {
    savedSsid = preferences.getString("ssid", "");
    savedPassword = preferences.getString("password", "");
    preferences.end();
  }

  if (savedSsid.isEmpty()) {
    startAccessPoint("no_saved_credentials");
  } else {
    startStationConnection();
    const unsigned long startedAtMs = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startedAtMs < WIFI_CONNECT_TIMEOUT_MS) {
      updateConnectingLed();
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) handleConnected();
    else startAccessPoint("connect_timeout");
  }
  Serial.printf("[ChainOSCnano][NET] web_server_started=%s\n",
                webServerStarted ? "true" : "false");
}

void networkUpdate() {
  if (webServerStarted) server.handleClient();
  if (networkState == NetworkState::AP_MODE) {
    dnsServer.processNextRequest();
  } else if (networkState == NetworkState::CONNECTING) {
    updateConnectingLed();
    if (WiFi.status() == WL_CONNECTED) handleConnected();
  } else if (networkState == NetworkState::CONNECTED &&
             WiFi.status() != WL_CONNECTED) {
    if (mdnsRunning) {
      MDNS.end();
      mdnsRunning = false;
    }
    WiFi.reconnect();
    networkState = NetworkState::CONNECTING;
    connectingLedOn = true;
    updateConnectingLed(true);
    Serial.println("[ChainOSCnano][NET] state=RECONNECTING");
  }

  if (restartScheduled && static_cast<long>(millis() - restartAtMs) >= 0) {
    Serial.println("[ChainOSCnano][NET] restarting=true");
    delay(20);
    ESP.restart();
  }
}
