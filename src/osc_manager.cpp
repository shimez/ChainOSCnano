#include "osc_manager.h"

#include <ArduinoOSCWiFi.h>
#include <Preferences.h>
#include <WiFi.h>
#include <math.h>

#include "config.h"
#include "angle_settings.h"
#include "encoder_settings.h"
#include "key_settings.h"
#include "joystick_settings.h"
#include "logging.h"
#include "tof_settings.h"

namespace {

String targetHost = "192.168.1.100";
uint16_t targetPort = 9000;

String uidText(const uint8_t* uid, size_t length) {
  String text;
  text.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    char byteText[3];
    snprintf(byteText, sizeof(byteText), "%02X", uid[index]);
    text += byteText;
  }
  return text;
}

void sendMessage(const KeySetting& setting, const KeyOscMessage& message) {
  if (message.valueType == TYPE_FLOAT) {
    OscWiFi.send(targetHost.c_str(), targetPort, message.address.c_str(),
                 message.valueStr.toFloat());
  } else if (message.valueType == TYPE_INT) {
    OscWiFi.send(targetHost.c_str(), targetPort, message.address.c_str(),
                 message.valueStr.toInt());
  } else {
    OscWiFi.send(targetHost.c_str(), targetPort, message.address.c_str(),
                 message.valueStr.c_str());
  }
  NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] source=%s address=%s value=%s target=%s:%u\n",
                setting.identity.c_str(), message.address.c_str(),
                message.valueStr.c_str(), targetHost.c_str(), targetPort);
}

void sendMessage(const String& identity, const KeyOscMessage& message) {
  if (message.valueType == TYPE_FLOAT)
    OscWiFi.send(targetHost.c_str(), targetPort, message.address.c_str(),
                 message.valueStr.toFloat());
  else if (message.valueType == TYPE_INT)
    OscWiFi.send(targetHost.c_str(), targetPort, message.address.c_str(),
                 message.valueStr.toInt());
  else
    OscWiFi.send(targetHost.c_str(), targetPort, message.address.c_str(),
                 message.valueStr.c_str());
  NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] source=%s address=%s value=%s target=%s:%u\n",
                identity.c_str(), message.address.c_str(),
                message.valueStr.c_str(), targetHost.c_str(), targetPort);
}

void sendKeyValue(KeySetting& setting, bool pressed) {
  if (WiFi.status() != WL_CONNECTED) {
    NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] source=%s skipped=wifi_disconnected\n",
                  setting.identity.c_str());
    return;
  }

  if (setting.mode == MODE_SEQUENCE) {
    if (!pressed) return;
    KeySequenceConfig& sequence = setting.sequence;
    const float value = sequence.current;
    const String valueText = sequence.valueType == TYPE_INT
                                 ? String(static_cast<int>(lroundf(value)))
                                 : String(value, 3);
    if (sequence.valueType == TYPE_FLOAT)
      OscWiFi.send(targetHost.c_str(), targetPort, sequence.address.c_str(), value);
    else if (sequence.valueType == TYPE_INT)
      OscWiFi.send(targetHost.c_str(), targetPort, sequence.address.c_str(),
                   static_cast<int>(lroundf(value)));
    else
      OscWiFi.send(targetHost.c_str(), targetPort, sequence.address.c_str(),
                   valueText.c_str());
    float next = value + sequence.step;
    if ((sequence.step >= 0 && next > sequence.end + 1e-6f) ||
        (sequence.step < 0 && next < sequence.end - 1e-6f))
      next = sequence.start;
    sequence.current = next;
    NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] source=%s mode=sequence address=%s value=%s target=%s:%u\n",
                  setting.identity.c_str(), sequence.address.c_str(),
                  valueText.c_str(), targetHost.c_str(), targetPort);
    return;
  }

  KeyOscMessage* messages = pressed ? setting.pressMessages : setting.releaseMessages;
  const uint8_t count = pressed ? setting.pressMessageCount : setting.releaseMessageCount;
  for (uint8_t index = 0; index < count; ++index) sendMessage(setting, messages[index]);
}

void sendEncoderClickValue(EncoderSetting& setting, bool pressed) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (setting.clickMode == MODE_SEQUENCE) {
    if (!pressed) return;
    KeySequenceConfig& sequence = setting.clickSequence;
    const float value = sequence.current;
    const String valueText = sequence.valueType == TYPE_INT
                                 ? String(static_cast<int>(lroundf(value)))
                                 : String(value, 3);
    if (sequence.valueType == TYPE_FLOAT)
      OscWiFi.send(targetHost.c_str(), targetPort, sequence.address.c_str(), value);
    else if (sequence.valueType == TYPE_INT)
      OscWiFi.send(targetHost.c_str(), targetPort, sequence.address.c_str(),
                   static_cast<int>(lroundf(value)));
    else
      OscWiFi.send(targetHost.c_str(), targetPort, sequence.address.c_str(),
                   valueText.c_str());
    float next = value + sequence.step;
    if ((sequence.step >= 0 && next > sequence.end + 1e-6f) ||
        (sequence.step < 0 && next < sequence.end - 1e-6f))
      next = sequence.start;
    sequence.current = next;
    return;
  }
  KeyOscMessage* messages = pressed ? setting.pressMessages
                                    : setting.releaseMessages;
  const uint8_t count = pressed ? setting.pressMessageCount
                                : setting.releaseMessageCount;
  for (uint8_t index = 0; index < count; ++index)
    sendMessage(setting.identity, messages[index]);
}

float clampValue(float value, float minimum, float maximum) {
  const float low = min(minimum, maximum);
  const float high = max(minimum, maximum);
  return constrain(value, low, high);
}

void sendEncoderRotationValue(EncoderSetting& setting, int16_t absoluteValue,
                              int16_t delta) {
  if (WiFi.status() != WL_CONNECTED) return;
  float mapped = 0;
  if (setting.sendIncrement) {
    mapped = clampValue(static_cast<float>(delta) * setting.incrementScale,
                        setting.outputMin, setting.outputMax);
  } else {
    const float span = setting.absoluteInputMax - setting.absoluteInputMin;
    float input = static_cast<float>(absoluteValue);
    if (fabsf(span) > 1e-6f) {
      input = fmodf(input - setting.absoluteInputMin, span);
      if (input < 0) input += span;
      input += setting.absoluteInputMin;
      const float ratio = (input - setting.absoluteInputMin) / span;
      mapped = setting.outputMin + ratio * (setting.outputMax - setting.outputMin);
    } else {
      mapped = setting.outputMin;
    }
    mapped = clampValue(mapped, setting.outputMin, setting.outputMax);
  }
  String valueText;
  if (setting.outputType == TYPE_INT) {
    const int value = static_cast<int>(lroundf(mapped));
    valueText = String(value);
    OscWiFi.send(targetHost.c_str(), targetPort,
                 setting.rotationAddress.c_str(), value);
  } else if (setting.outputType == TYPE_STRING) {
    valueText = String(mapped, 3);
    OscWiFi.send(targetHost.c_str(), targetPort,
                 setting.rotationAddress.c_str(), valueText.c_str());
  } else {
    valueText = String(mapped, 3);
    OscWiFi.send(targetHost.c_str(), targetPort,
                 setting.rotationAddress.c_str(), mapped);
  }
  NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] source=%s rotation=%s value=%s target=%s:%u\n",
                setting.identity.c_str(), setting.rotationAddress.c_str(),
                valueText.c_str(), targetHost.c_str(), targetPort);
}

void sendAngleValue(AngleSetting& setting, int rawValue) {
  if (WiFi.status() != WL_CONNECTED) return;
  const float inputMax = setting.use12Bit ? 4095.0f : 255.0f;
  const float ratio = constrain(static_cast<float>(rawValue) / inputMax,
                                0.0f, 1.0f);
  const float mapped = setting.outputMin +
                       ratio * (setting.outputMax - setting.outputMin);
  String valueText;
  if (setting.outputType == TYPE_INT) {
    const int value = static_cast<int>(lroundf(mapped));
    valueText = String(value);
    OscWiFi.send(targetHost.c_str(), targetPort, setting.address.c_str(), value);
  } else if (setting.outputType == TYPE_STRING) {
    valueText = String(mapped, 3);
    OscWiFi.send(targetHost.c_str(), targetPort, setting.address.c_str(),
                 valueText.c_str());
  } else {
    valueText = String(mapped, 3);
    OscWiFi.send(targetHost.c_str(), targetPort, setting.address.c_str(), mapped);
  }
  NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] source=%s angle=%s raw=%d value=%s target=%s:%u\n",
                setting.identity.c_str(), setting.address.c_str(), rawValue,
                valueText.c_str(), targetHost.c_str(), targetPort);
}

void sendTofValue(TofSetting& setting, int distanceMm) {
  if (WiFi.status() != WL_CONNECTED) return;
  const float ratio = constrain((distanceMm - 30.0f) /
      (static_cast<float>(setting.maxDistanceMm) - 30.0f), 0.0f, 1.0f);
  const float directed = setting.nearValueHigh ? 1.0f - ratio : ratio;
  const float mapped = setting.outputMin +
                       directed * (setting.outputMax - setting.outputMin);
  String valueText;
  if (setting.outputType == TYPE_INT) {
    const int value = static_cast<int>(lroundf(mapped)); valueText = String(value);
    OscWiFi.send(targetHost.c_str(), targetPort, setting.address.c_str(), value);
  } else {
    valueText = String(mapped, 3);
    OscWiFi.send(targetHost.c_str(), targetPort, setting.address.c_str(), mapped);
  }
  NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] source=%s tof=%s mm=%d value=%s target=%s:%u\n",
                setting.identity.c_str(), setting.address.c_str(), distanceMm,
                valueText.c_str(), targetHost.c_str(), targetPort);
}

void sendJoystickClickValue(JoystickSetting& setting, bool pressed) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (setting.clickMode == MODE_SEQUENCE) {
    if (!pressed) return;
    KeySequenceConfig& sequence = setting.clickSequence;
    const float value = sequence.current;
    if (sequence.valueType == TYPE_FLOAT) OscWiFi.send(targetHost.c_str(), targetPort, sequence.address.c_str(), value);
    else if (sequence.valueType == TYPE_INT) OscWiFi.send(targetHost.c_str(), targetPort, sequence.address.c_str(), static_cast<int>(lroundf(value)));
    else { const String text(value, 3); OscWiFi.send(targetHost.c_str(), targetPort, sequence.address.c_str(), text.c_str()); }
    float next = value + sequence.step;
    if ((sequence.step >= 0 && next > sequence.end + 1e-6f) || (sequence.step < 0 && next < sequence.end - 1e-6f)) next = sequence.start;
    sequence.current = next;
    return;
  }
  KeyOscMessage* messages = pressed ? setting.pressMessages : setting.releaseMessages;
  const uint8_t count = pressed ? setting.pressMessageCount : setting.releaseMessageCount;
  for (uint8_t i = 0; i < count; ++i) sendMessage(setting.identity, messages[i]);
}

void sendJoystickAxis(const JoystickSetting& setting, const String& address,
                      int8_t raw, bool invert, const char* axis) {
  if (WiFi.status() != WL_CONNECTED) return;
  const float input = invert ? -static_cast<float>(raw) : static_cast<float>(raw);
  const float ratio = constrain((input + 127.0f) / 254.0f, 0.0f, 1.0f);
  const float mapped = setting.outputMin + ratio * (setting.outputMax - setting.outputMin);
  if (setting.outputType == TYPE_INT) OscWiFi.send(targetHost.c_str(), targetPort, address.c_str(), static_cast<int>(lroundf(mapped)));
  else if (setting.outputType == TYPE_STRING) { const String text(mapped, 3); OscWiFi.send(targetHost.c_str(), targetPort, address.c_str(), text.c_str()); }
  else OscWiFi.send(targetHost.c_str(), targetPort, address.c_str(), mapped);
  NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] source=%s joystick_%s=%s raw=%d\n", setting.identity.c_str(), axis, address.c_str(), raw);
}

}  // namespace

void oscSetup() {
  keySettingsSetup();
  encoderSettingsSetup();
  angleSettingsSetup();
  tofSettingsSetup();
  joystickSettingsSetup();
  Preferences preferences;
  if (preferences.begin(WIFI_PREFS_NAMESPACE, true)) {
    targetHost = preferences.getString("osc_host", targetHost);
    const uint32_t storedPort = preferences.getUInt("osc_port", targetPort);
    if (storedPort >= 1 && storedPort <= 65535) {
      targetPort = static_cast<uint16_t>(storedPort);
    }
    preferences.end();
  }
  NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] target=%s:%u\n", targetHost.c_str(),
                targetPort);
}

const String& oscTargetHost() { return targetHost; }

uint16_t oscTargetPort() { return targetPort; }

bool oscSaveTarget(const String& host, uint16_t port) {
  if (host.isEmpty() || host.length() > 253 || port == 0) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(WIFI_PREFS_NAMESPACE, false)) {
    return false;
  }
  const size_t hostWritten = preferences.putString("osc_host", host);
  const size_t portWritten = preferences.putUInt("osc_port", port);
  preferences.end();
  if (hostWritten == 0 || portWritten == 0) {
    return false;
  }
  targetHost = host;
  targetPort = port;
  NANO_VERBOSE_LOGF("[ChainOSCnano][OSC] target_saved=%s:%u\n",
                targetHost.c_str(), targetPort);
  return true;
}

void oscSendDualKey(uint8_t keyNumber, bool pressed) {
  const String identity = String("dualkey:") + keyNumber;
  KeySetting* setting = keySettingsEnsure(
      identity, String("DualKey KEY") + keyNumber,
      String("/chainoscnano/dualkey/key") + keyNumber);
  if (setting != nullptr) sendKeyValue(*setting, pressed);
}

void oscBeginChainPortUpdate(uint8_t portMask) {
  keySettingsBeginPortUpdate(portMask);
  encoderSettingsBeginPortUpdate(portMask);
  angleSettingsBeginPortUpdate(portMask);
  tofSettingsBeginPortUpdate(portMask);
  joystickSettingsBeginPortUpdate(portMask);
}

void oscRegisterChainKey(const uint8_t* uidBytes, size_t uidLength,
                         uint8_t portMask) {
  const String uid = uidText(uidBytes, uidLength);
  const String identity = String("chain:") + uid;
  keySettingsEnsure(identity, String("Chain Key ") + uid,
                    String("/chainoscnano/chain/key/") + uid);
  keySettingsMarkConnected(identity, portMask);
}

void oscSendChainKey(const uint8_t* uidBytes, size_t uidLength, bool pressed) {
  const String uid = uidText(uidBytes, uidLength);
  KeySetting* setting = keySettingsEnsure(
      String("chain:") + uid, String("Chain Key ") + uid,
      String("/chainoscnano/chain/key/") + uid);
  if (setting != nullptr) sendKeyValue(*setting, pressed);
}

void oscRegisterChainEncoder(const uint8_t* uidBytes, size_t uidLength,
                             uint8_t portMask) {
  const String uid = uidText(uidBytes, uidLength);
  const String identity = String("chain:") + uid;
  encoderSettingsEnsure(identity, String("Chain Encoder ") + uid);
  encoderSettingsMarkConnected(identity, portMask);
}

void oscSendChainEncoderRotation(const uint8_t* uidBytes, size_t uidLength,
                                 int16_t absoluteValue, int16_t delta) {
  const String uid = uidText(uidBytes, uidLength);
  EncoderSetting* setting = encoderSettingsEnsure(
      String("chain:") + uid, String("Chain Encoder ") + uid);
  if (setting) sendEncoderRotationValue(*setting, absoluteValue, delta);
}

bool oscSendChainEncoderClick(const uint8_t* uidBytes, size_t uidLength,
                              bool pressed) {
  const String uid = uidText(uidBytes, uidLength);
  EncoderSetting* setting = encoderSettingsEnsure(
      String("chain:") + uid, String("Chain Encoder ") + uid);
  if (setting) {
    sendEncoderClickValue(*setting, pressed);
    return setting->clickMode == MODE_SEQUENCE;
  }
  return false;
}

void oscRegisterChainAngle(const uint8_t* uidBytes, size_t uidLength,
                           uint8_t portMask) {
  const String uid = uidText(uidBytes, uidLength);
  const String identity = String("chain:") + uid;
  angleSettingsEnsure(identity, String("Chain Angle ") + uid);
  angleSettingsMarkConnected(identity, portMask);
}

void oscSendChainAngle(const uint8_t* uidBytes, size_t uidLength,
                       int rawValue) {
  const String uid = uidText(uidBytes, uidLength);
  AngleSetting* setting = angleSettingsEnsure(
      String("chain:") + uid, String("Chain Angle ") + uid);
  if (setting) sendAngleValue(*setting, rawValue);
}

void oscRegisterChainTof(const uint8_t* uidBytes, size_t uidLength,
                         uint8_t portMask) {
  const String uid = uidText(uidBytes, uidLength);
  const String identity = String("chain:") + uid;
  tofSettingsEnsure(identity, String("Chain ToF ") + uid);
  tofSettingsMarkConnected(identity, portMask);
}

void oscSendChainTof(const uint8_t* uidBytes, size_t uidLength,
                     int distanceMm) {
  const String uid = uidText(uidBytes, uidLength);
  TofSetting* setting = tofSettingsEnsure(
      String("chain:") + uid, String("Chain ToF ") + uid);
  if (setting) sendTofValue(*setting, distanceMm);
}

void oscRegisterChainJoystick(const uint8_t* uidBytes, size_t uidLength,
                              uint8_t portMask) {
  const String uid = uidText(uidBytes, uidLength), identity = String("chain:") + uid;
  joystickSettingsEnsure(identity, String("Chain Joystick ") + uid);
  joystickSettingsMarkConnected(identity, portMask);
}

void oscSendChainJoystickAxes(const uint8_t* uidBytes, size_t uidLength,
                              int8_t x, int8_t y, uint8_t portMask,
                              bool xChanged, bool yChanged) {
  const String uid = uidText(uidBytes, uidLength);
  JoystickSetting* setting = joystickSettingsEnsure(String("chain:") + uid, String("Chain Joystick ") + uid);
  if (!setting) return;
  if (portMask == 0x02) {
    x = static_cast<int8_t>(constrain(-static_cast<int16_t>(x), -127, 127));
    y = static_cast<int8_t>(constrain(-static_cast<int16_t>(y), -127, 127));
  }
  if (xChanged) sendJoystickAxis(*setting, setting->xAddress, x, setting->invertX, "x");
  if (yChanged) sendJoystickAxis(*setting, setting->yAddress, y, setting->invertY, "y");
}

bool oscSendChainJoystickClick(const uint8_t* uidBytes, size_t uidLength,
                               bool pressed) {
  const String uid = uidText(uidBytes, uidLength);
  JoystickSetting* setting = joystickSettingsEnsure(String("chain:") + uid, String("Chain Joystick ") + uid);
  if (!setting) return false;
  sendJoystickClickValue(*setting, pressed);
  return setting->clickMode == MODE_SEQUENCE;
}
