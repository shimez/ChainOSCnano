#include "osc_manager.h"

#include <ArduinoOSCWiFi.h>
#include <Preferences.h>
#include <WiFi.h>

#include "config.h"

namespace {

String targetHost = OSC_DEFAULT_HOST;
uint16_t targetPort = OSC_DEFAULT_PORT;

String keyAddress(const uint8_t* uid, size_t uidLength) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  String address = F("/chainoscnano/key/");
  address.reserve(address.length() + uidLength * 2);
  for (size_t index = 0; index < uidLength; ++index) {
    address += HEX[(uid[index] >> 4) & 0x0F];
    address += HEX[uid[index] & 0x0F];
  }
  return address;
}

}  // namespace

void oscSetup() {
  Preferences preferences;
  if (preferences.begin(WIFI_PREFS_NAMESPACE, true)) {
    targetHost = preferences.getString("osc_host", OSC_DEFAULT_HOST);
    const uint32_t storedPort =
        preferences.getUInt("osc_port", OSC_DEFAULT_PORT);
    if (storedPort >= 1 && storedPort <= 65535) {
      targetPort = static_cast<uint16_t>(storedPort);
    }
    preferences.end();
  }
  Serial.printf("[ChainOSCnano][OSC] target=%s:%u\n", targetHost.c_str(),
                targetPort);
}

const String& oscTargetHost() { return targetHost; }

uint16_t oscTargetPort() { return targetPort; }

bool oscSaveTarget(const String& host, uint16_t port) {
  Preferences preferences;
  if (!preferences.begin(WIFI_PREFS_NAMESPACE, false)) return false;
  const size_t hostWritten = preferences.putString("osc_host", host);
  const size_t portWritten = preferences.putUInt("osc_port", port);
  preferences.end();
  if (hostWritten == 0 || portWritten == 0) return false;
  targetHost = host;
  targetPort = port;
  Serial.printf("[ChainOSCnano][OSC] target_saved=%s:%u\n",
                targetHost.c_str(), targetPort);
  return true;
}

void oscSendKeyEvent(const uint8_t* uid, size_t uidLength, bool pressed) {
  const String address = keyAddress(uid, uidLength);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf(
        "[ChainOSCnano][OSC] skipped reason=wifi_disconnected address=%s value=%d\n",
        address.c_str(), pressed ? 1 : 0);
    return;
  }
  OscWiFi.send(targetHost.c_str(), targetPort, address.c_str(), pressed ? 1 : 0);
  Serial.printf(
      "[ChainOSCnano][OSC] sent target=%s:%u address=%s type=Int value=%d\n",
      targetHost.c_str(), targetPort, address.c_str(), pressed ? 1 : 0);
}
