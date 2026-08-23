#include "osc_manager.h"

#include <ArduinoOSCWiFi.h>
#include <Preferences.h>
#include <WiFi.h>

#include "config.h"
#include "key_settings.h"

namespace {

String targetHost = OSC_DEFAULT_HOST;
uint16_t targetPort = OSC_DEFAULT_PORT;

String uidString(const uint8_t* uid, size_t uidLength) {
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  String value;
  value.reserve(uidLength * 2);
  for (size_t index = 0; index < uidLength; ++index) {
    value += HEX_DIGITS[(uid[index] >> 4) & 0x0F];
    value += HEX_DIGITS[uid[index] & 0x0F];
  }
  return value;
}

void sendMessage(const KeyOscMessage& message) {
  if (message.type == TYPE_FLOAT) {
    OscWiFi.send(targetHost.c_str(), targetPort, message.address.c_str(),
                 message.value.toFloat());
  } else if (message.type == TYPE_INT) {
    OscWiFi.send(targetHost.c_str(), targetPort, message.address.c_str(),
                 static_cast<int32_t>(strtol(message.value.c_str(), nullptr, 10)));
  } else {
    OscWiFi.send(targetHost.c_str(), targetPort, message.address.c_str(),
                 message.value.c_str());
  }
  Serial.printf("[ChainOSCnano][OSC] sent address=%s type=%u value=%s target=%s:%u\n",
                message.address.c_str(), message.type, message.value.c_str(),
                targetHost.c_str(), targetPort);
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
  const String uidText = uidString(uid, uidLength);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf(
        "[ChainOSCnano][OSC] skipped reason=wifi_disconnected uid=%s state=%s\n",
        uidText.c_str(), pressed ? "PRESSED" : "RELEASED");
    return;
  }
  KeySetting* setting = keySettingsEnsure(uidText);
  if (!setting) {
    Serial.printf("[ChainOSCnano][OSC] skipped reason=no_key_setting uid=%s\n",
                  uidText.c_str());
    return;
  }
  if (setting->mode == MODE_SEQUENCE) {
    if (!pressed) return;
    KeyOscMessage message;
    message.address = setting->sequence.address;
    message.type = setting->sequence.type;
    message.value = message.type == TYPE_INT
                        ? String(static_cast<int32_t>(lroundf(setting->sequence.current)))
                        : String(setting->sequence.current, 3);
    sendMessage(message);
    float next = setting->sequence.current + setting->sequence.step;
    if ((setting->sequence.step > 0 && next > setting->sequence.end + 0.000001f) ||
        (setting->sequence.step < 0 && next < setting->sequence.end - 0.000001f))
      next = setting->sequence.start;
    setting->sequence.current = next;
    return;
  }
  KeyOscMessage* messages = pressed ? setting->press : setting->release;
  const uint8_t count = pressed ? setting->pressCount : setting->releaseCount;
  for (uint8_t index = 0; index < count; ++index) sendMessage(messages[index]);
}
