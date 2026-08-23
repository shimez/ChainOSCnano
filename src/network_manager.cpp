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
  String accepted = server.header("Accept-Language");
  accepted.toLowerCase();
  return accepted.startsWith("ja");
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
  html += F("</title><style>body{font-family:sans-serif;margin:16px;background:#f5f5f5;color:#18212f}main{max-width:680px;margin:auto}.card{background:#fff;padding:18px;border-radius:10px;margin-bottom:16px;box-shadow:0 2px 5px rgba(0,0,0,.1)}h1{font-size:1.45em}h2{font-size:1.1em}label{display:block;margin-top:12px;font-weight:bold}input{width:100%;padding:10px;margin-top:5px;box-sizing:border-box;font-size:1em}button{width:100%;padding:12px;margin-top:16px;border:0;border-radius:6px;background:#3267e3;color:#fff;font-size:1em}.danger{background:#dc3545}.status{padding:11px;background:#edf3ff;color:#244da7;border-radius:8px}.note{color:#667085;line-height:1.5;word-break:break-word}</style></head><body><main>");
  return html;
}

void sendPage(String html) {
  html += F("</main></body></html>");
  server.send(200, "text/html; charset=utf-8", html);
}

void sendProvisioningPage(const String& error = "") {
  const bool japanese = isJapaneseRequest();
  String html = pageStart("ChainOSCnano Wi-Fi Setup", japanese);
  html += F("<h1>ChainOSCnano</h1><div class='card'><h2>");
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
  html += F("<h1>ChainOSCnano</h1><div class='card'><h2>");
  html += tr(japanese, "Network status", "ネットワーク状態");
  html += F("</h2><p class='status'>");
  html += tr(japanese, "Wi-Fi connected", "Wi-Fi接続済み");
  html += F("</p><p class='note'>IP: ");
  html += WiFi.localIP().toString();
  html += F("<br>mDNS: http://");
  html += WIFI_MDNS_HOST;
  html += F(".local/<br>Version: ");
  html += APP_VERSION;
  html += F("</p></div><div class='card'><h2>");
  html += tr(japanese, "OSC destination", "OSC送信先");
  html += F("</h2>");
  if (!message.isEmpty()) {
    html += error ? F("<p style='color:#c73c4a'>") : F("<p class='status'>");
    html += htmlEscape(message);
    html += F("</p>");
  }
  html += F("<form method='POST' action='/save-osc'><label>");
  html += tr(japanese, "IPv4 address", "IPv4アドレス");
  html += F("</label><input name='host' inputmode='decimal' maxlength='15' required value='");
  html += htmlEscape(oscTargetHost());
  html += F("'><label>");
  html += tr(japanese, "UDP port", "UDPポート");
  html += F("</label><input name='port' type='number' min='1' max='65535' required value='");
  html += String(oscTargetPort());
  html += F("'><button type='submit'>");
  html += tr(japanese, "Save OSC destination", "OSC送信先を保存");
  html += F("</button></form><p class='note'>");
  html += tr(japanese,
             "Each Chain Key sends Int 1 when pressed and Int 0 when released.",
             "各Chain Keyは、押した時にInt 1、離した時にInt 0を送信します。");
  html += F("</p></div><div class='card'><h2>");
  html += tr(japanese, "Wi-Fi settings", "Wi-Fi設定");
  html += F("</h2><form method='POST' action='/forget-wifi'><button class='danger' type='submit'>");
  html += tr(japanese, "Delete Wi-Fi settings", "Wi-Fi設定を削除");
  html += F("</button></form></div>");
  sendPage(html);
}

void handleRoot() {
  if (networkState == NetworkState::AP_MODE) sendProvisioningPage();
  else sendConnectedPage();
}

void handleSaveOsc() {
  const bool japanese = isJapaneseRequest();
  String host = server.arg("host");
  host.trim();
  const String portText = server.arg("port");
  IPAddress parsedAddress;
  bool portDigitsOnly = !portText.isEmpty();
  for (size_t index = 0; portDigitsOnly && index < portText.length(); ++index) {
    portDigitsOnly = isdigit(static_cast<unsigned char>(portText[index]));
  }
  const unsigned long parsedPort = portText.toInt();
  if (!parsedAddress.fromString(host)) {
    sendConnectedPage(
        tr(japanese, "Enter a valid IPv4 address.",
           "正しいIPv4アドレスを入力してください。"),
        true);
    return;
  }
  if (!portDigitsOnly || parsedPort < 1 || parsedPort > 65535) {
    sendConnectedPage(
        tr(japanese, "UDP port must be between 1 and 65535.",
           "UDPポートは1～65535で入力してください。"),
        true);
    return;
  }
  if (!oscSaveTarget(host, static_cast<uint16_t>(parsedPort))) {
    sendConnectedPage(
        tr(japanese, "Could not save the OSC destination.",
           "OSC送信先を保存できませんでした。"),
        true);
    return;
  }
  sendConnectedPage(
      tr(japanese, "OSC destination saved.", "OSC送信先を保存しました。"));
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
  server.on("/save-wifi", HTTP_POST, handleSaveWifi);
  server.on("/forget-wifi", HTTP_POST, handleForgetWifi);
  server.on("/save-osc", HTTP_POST, handleSaveOsc);
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
