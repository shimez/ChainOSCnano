#include "chain_probe.h"

#include <Arduino.h>
#include <M5Chain.h>
#include <string.h>

#include "config.h"
#include "logging.h"
#include "angle_settings.h"
#include "joystick_settings.h"
#include "osc_manager.h"
#include "tof_settings.h"

namespace {

constexpr size_t UID_SIZE = 12;

struct DeviceSnapshot {
  uint16_t id;
  chain_device_type_t type;
  uint8_t uid[UID_SIZE];
  bool uidValid;
  uint8_t lastButtonStatus;
  bool buttonInitialized;
  bool keyReadErrorReported;
  int16_t lastEncoderAbsolute;
  bool encoderInitialized;
  bool encoderReadErrorReported;
  int lastAngleValue;
  bool angleInitialized;
  bool angleReadErrorReported;
  int8_t lastJoystickX;
  int8_t lastJoystickY;
  bool joystickInitialized;
  bool joystickReadErrorReported;
  int lastTofMm;
  bool tofInitialized;
  bool tofConfigured;
  uint8_t tofReadFailures;
  unsigned long lastTofConfigMs;
  unsigned long lastTofPollMs;
  unsigned long identifyUntilMs;
};

struct ChainPortContext {
  const char* name;
  HardwareSerial* serial;
  uint8_t rxPin;
  uint8_t txPin;
  uint8_t portMask;
  Chain bus;
  DeviceSnapshot devices[CHAIN_MAX_DEVICES];
  uint16_t deviceCount;
  bool connected;
  bool firstScan;
  unsigned long lastScanMs;
  unsigned long lastKeyPollMs;

  ChainPortContext(const char* portName, HardwareSerial* hardwareSerial,
                   uint8_t rx, uint8_t tx, uint8_t mask)
      : name(portName),
        serial(hardwareSerial),
        rxPin(rx),
        txPin(tx),
        portMask(mask),
        devices{},
        deviceCount(0),
        connected(false),
        firstScan(true),
        lastScanMs(0),
        lastKeyPollMs(0) {}
};

ChainPortContext portG1G2("G1_G2", &Serial1, CHAIN_G1_G2_RX_PIN,
                          CHAIN_G1_G2_TX_PIN, 0x01);

uint8_t colorBlue[] = {0, 0, 255};
uint8_t colorOrange[] = {255, 64, 0};
uint8_t colorRed[] = {255, 0, 0};
uint8_t colorGreen[] = {0, 255, 0};

bool hasStatusLed(chain_device_type_t type) {
  return type == CHAIN_KEY_TYPE_CODE || type == CHAIN_ENCODER_TYPE_CODE ||
         type == CHAIN_ANGLE_TYPE_CODE || type == CHAIN_JOYSTICK_TYPE_CODE ||
         type == CHAIN_TOF_TYPE_CODE;
}

bool identifyActive(const DeviceSnapshot& device, unsigned long now) {
  return device.identifyUntilMs != 0 &&
         static_cast<long>(device.identifyUntilMs - now) > 0;
}

const char* chainStatusName(chain_status_t status) {
  switch (status) {
    case CHAIN_OK: return "OK";
    case CHAIN_PARAMETER_ERROR: return "PARAMETER_ERROR";
    case CHAIN_RETURN_PACKET_ERROR: return "RETURN_PACKET_ERROR";
    case CHAIN_BUSY: return "BUSY";
    case CHAIN_TIMEOUT: return "TIMEOUT";
    default: return "UNKNOWN";
  }
}

const char* deviceTypeName(chain_device_type_t type) {
  switch (type) {
    case CHAIN_ENCODER_TYPE_CODE: return "Encoder";
    case CHAIN_ANGLE_TYPE_CODE: return "Angle";
    case CHAIN_KEY_TYPE_CODE: return "Key";
    case CHAIN_JOYSTICK_TYPE_CODE: return "Joystick";
    case CHAIN_TOF_TYPE_CODE: return "ToF";
    case UNIT_CHAIN_BUS_TYPE_CODE: return "UnitChainBus";
    case CHAIN_SWITCH_TYPE_CODE: return "Switch";
    case CHAIN_PIR_TYPE_CODE: return "PIR";
    case CHAIN_MIC_TYPE_CODE: return "Microphone";
    case CHAIN_BUZZER_TYPE_CODE: return "Buzzer";
    case UNIT_8SERVOS2_CHAIN_TYPE_CODE: return "8Servos2";
    case CHAIN_MONO_TYPE_CODE: return "Mono";
    case CHAIN_RGB_TYPE_CODE: return "RGB";
    case CHAIN_UNKNOWN_TYPE_CODE:
    default: return "Unknown";
  }
}

void printUid(const DeviceSnapshot& device) {
  if (!device.uidValid) {
    NANO_VERBOSE_PRINT("unavailable");
    return;
  }
  for (size_t index = 0; index < UID_SIZE; ++index) {
    NANO_VERBOSE_LOGF("%02X", device.uid[index]);
  }
}

bool sameDevice(const DeviceSnapshot& left, const DeviceSnapshot& right) {
  if (left.id != right.id || left.type != right.type ||
      left.uidValid != right.uidValid) {
    return false;
  }
  return !left.uidValid || memcmp(left.uid, right.uid, UID_SIZE) == 0;
}

bool snapshotChanged(const ChainPortContext& port,
                     const DeviceSnapshot* devices, uint16_t count) {
  if (!port.connected || count != port.deviceCount) {
    return true;
  }
  for (uint16_t index = 0; index < count; ++index) {
    if (!sameDevice(devices[index], port.devices[index])) {
      return true;
    }
  }
  return false;
}

void saveSnapshot(ChainPortContext& port, const DeviceSnapshot* devices,
                  uint16_t count) {
  port.deviceCount = count;
  for (uint16_t index = 0; index < count; ++index) {
    port.devices[index] = devices[index];
  }
}

void printSnapshot(const ChainPortContext& port,
                   const DeviceSnapshot* devices, uint16_t count) {
  NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN][%s] devices=%u\n", port.name, count);
  for (uint16_t index = 0; index < count; ++index) {
    NANO_VERBOSE_LOGF(
        "[ChainOSCnano][CHAIN][%s] index=%u id=%u type=%u(%s) uid=",
        port.name, index, devices[index].id,
        static_cast<unsigned int>(devices[index].type),
        deviceTypeName(devices[index].type));
    printUid(devices[index]);
    NANO_VERBOSE_PRINTLN();
  }
}

void drainKeyReports(ChainPortContext& port, uint16_t id) {
  chain_button_press_type_t ignoredType;
  while (port.bus.getKeyButtonPressStatus(id, &ignoredType)) {
    // Raw pressed/released state is used. Active reports are drained because
    // M5Chain stores each one in a dynamically allocated linked-list node.
  }
}

void drainAllKeyReports(ChainPortContext& port) {
  for (uint16_t id = 1; id <= CHAIN_MAX_DEVICES; ++id) {
    drainKeyReports(port, id);
  }
}

bool setKeyLed(ChainPortContext& port, DeviceSnapshot& device,
               uint8_t* color, const char* colorName) {
  if (identifyActive(device, millis())) return true;
  uint8_t operationStatus = 0;
  const chain_status_t status = port.bus.setRGBValue(
      device.id, 0, 1, color, 3, &operationStatus, 100);
  if (status == CHAIN_OK && operationStatus != 0) {
    return true;
  }
  NANO_VERBOSE_LOGF(
      "[ChainOSCnano][CHAIN_KEY][%s] led_error id=%u color=%s "
      "status=%d(%s) operation=%u\n",
      port.name, device.id, colorName, static_cast<int>(status),
      chainStatusName(status), operationStatus);
  return false;
}

bool setDeviceLed(ChainPortContext& port, DeviceSnapshot& device,
                  uint8_t* color, const char* colorName) {
  return setKeyLed(port, device, color, colorName);
}

void updateIdentifyLeds(ChainPortContext& port) {
  const unsigned long now = millis();
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (device.identifyUntilMs == 0 || identifyActive(device, now)) continue;
    device.identifyUntilMs = 0;
    setDeviceLed(port, device, colorBlue, "BLUE");
  }
}

bool identifyOnPort(ChainPortContext& port, const String& identity) {
  if (!identity.startsWith("chain:")) return false;
  const String requestedUid = identity.substring(6);
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (!device.uidValid || !hasStatusLed(device.type)) continue;
    String uid;
    uid.reserve(UID_SIZE * 2);
    for (size_t byteIndex = 0; byteIndex < UID_SIZE; ++byteIndex) {
      char byteText[3];
      snprintf(byteText, sizeof(byteText), "%02X", device.uid[byteIndex]);
      uid += byteText;
    }
    if (uid != requestedUid) continue;
    uint8_t operationStatus = 0;
    port.bus.setRGBLight(device.id, CHAIN_KEY_LED_BRIGHTNESS,
                         &operationStatus, CHAIN_SAVE_FLASH_DISABLE, 100);
    if (!setDeviceLed(port, device, colorOrange, "ORANGE")) return false;
    device.identifyUntilMs = millis() + 10000UL;
    return true;
  }
  return false;
}

void initializeKey(ChainPortContext& port, DeviceSnapshot& device) {
  uint8_t operationStatus = 0;
  const chain_status_t brightnessStatus = port.bus.setRGBLight(
      device.id, CHAIN_KEY_LED_BRIGHTNESS, &operationStatus,
      CHAIN_SAVE_FLASH_DISABLE, 100);
  if (brightnessStatus != CHAIN_OK || operationStatus == 0) {
    NANO_VERBOSE_LOGF(
        "[ChainOSCnano][CHAIN_KEY][%s] brightness_error id=%u "
        "status=%d(%s) operation=%u\n",
        port.name, device.id, static_cast<int>(brightnessStatus),
        chainStatusName(brightnessStatus), operationStatus);
  }

  uint8_t rawStatus = 0;
  const chain_status_t status =
      port.bus.getKeyButtonStatus(device.id, &rawStatus, 100);
  drainKeyReports(port, device.id);
  if (status != CHAIN_OK) {
    NANO_VERBOSE_LOGF(
        "[ChainOSCnano][CHAIN_KEY][%s] init_error id=%u status=%d(%s)\n",
        port.name, device.id, static_cast<int>(status),
        chainStatusName(status));
    device.buttonInitialized = false;
    return;
  }

  device.lastButtonStatus = rawStatus != 0 ? 1 : 0;
  device.buttonInitialized = true;
  device.keyReadErrorReported = false;
  setKeyLed(port, device,
            device.lastButtonStatus != 0 ? colorOrange : colorBlue,
            device.lastButtonStatus != 0 ? "ORANGE" : "BLUE");

  NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_KEY][%s] ready id=%u uid=", port.name,
                device.id);
  printUid(device);
  NANO_VERBOSE_LOGF(" initial=%s led=%s\n",
                device.lastButtonStatus != 0 ? "PRESSED" : "RELEASED",
                device.lastButtonStatus != 0 ? "ORANGE" : "BLUE");
}

void initializeKeys(ChainPortContext& port) {
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    if (port.devices[index].type == CHAIN_KEY_TYPE_CODE) {
      initializeKey(port, port.devices[index]);
    }
  }
}

void initializeEncoder(ChainPortContext& port, DeviceSnapshot& device) {
  uint8_t operationStatus = 0;
  port.bus.setRGBLight(device.id, CHAIN_KEY_LED_BRIGHTNESS, &operationStatus,
                       CHAIN_SAVE_FLASH_DISABLE, 100);
  int16_t absoluteValue = 0;
  uint8_t buttonStatus = 0;
  const chain_status_t valueResult =
      port.bus.getEncoderValue(device.id, &absoluteValue);
  const chain_status_t buttonResult =
      port.bus.getEncoderButtonStatus(device.id, &buttonStatus);
  if (valueResult != CHAIN_OK || buttonResult != CHAIN_OK) {
    NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_ENCODER][%s] init_error id=%u value=%s button=%s\n",
                  port.name, device.id, chainStatusName(valueResult),
                  chainStatusName(buttonResult));
    return;
  }
  device.lastEncoderAbsolute = absoluteValue;
  device.encoderInitialized = true;
  device.lastButtonStatus = buttonStatus != 0 ? 1 : 0;
  device.buttonInitialized = true;
  device.encoderReadErrorReported = false;
  setDeviceLed(port, device, colorBlue, "BLUE");
  NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_ENCODER][%s] ready id=%u value=%d uid=",
                port.name, device.id, static_cast<int>(absoluteValue));
  printUid(device);
  NANO_VERBOSE_PRINTLN();
}

void initializeEncoders(ChainPortContext& port) {
  for (uint16_t index = 0; index < port.deviceCount; ++index)
    if (port.devices[index].type == CHAIN_ENCODER_TYPE_CODE)
      initializeEncoder(port, port.devices[index]);
}

void initializeAngles(ChainPortContext& port) {
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (device.type != CHAIN_ANGLE_TYPE_CODE) continue;
    device.angleInitialized = false;
    device.angleReadErrorReported = false;
    setDeviceLed(port, device, colorBlue, "BLUE");
    NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_ANGLE][%s] ready id=%u uid=",
                  port.name, device.id);
    printUid(device);
    NANO_VERBOSE_PRINTLN();
  }
}

void initializeTofs(ChainPortContext& port) {
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (device.type != CHAIN_TOF_TYPE_CODE) continue;
    device.lastTofMm = -1; device.tofInitialized = false;
    device.tofConfigured = false; device.tofReadFailures = 0;
    device.lastTofConfigMs = 0; device.lastTofPollMs = 0;
    setDeviceLed(port, device, colorBlue, "BLUE");
  }
}

void initializeJoysticks(ChainPortContext& port) {
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (device.type != CHAIN_JOYSTICK_TYPE_CODE) continue;
    device.joystickInitialized = false;
    device.joystickReadErrorReported = false;
    device.buttonInitialized = false;
    setDeviceLed(port, device, colorBlue, "BLUE");
    NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_JOYSTICK][%s] ready id=%u uid=",
                  port.name, device.id);
    printUid(device);
    NANO_VERBOSE_PRINTLN();
  }
}

void pollKeys(ChainPortContext& port) {
  if (!port.connected) {
    return;
  }

  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (device.type != CHAIN_KEY_TYPE_CODE) {
      continue;
    }

    uint8_t rawStatus = 0;
    const chain_status_t status =
        port.bus.getKeyButtonStatus(device.id, &rawStatus, 50);
    drainKeyReports(port, device.id);
    if (status != CHAIN_OK) {
      if (!device.keyReadErrorReported) {
        NANO_VERBOSE_LOGF(
            "[ChainOSCnano][CHAIN_KEY][%s] read_error id=%u "
            "status=%d(%s)\n",
            port.name, device.id, static_cast<int>(status),
            chainStatusName(status));
        device.keyReadErrorReported = true;
      }
      continue;
    }

    device.keyReadErrorReported = false;
    const uint8_t buttonStatus = rawStatus != 0 ? 1 : 0;
    if (!device.buttonInitialized) {
      device.lastButtonStatus = buttonStatus;
      device.buttonInitialized = true;
      setKeyLed(port, device, buttonStatus != 0 ? colorOrange : colorBlue,
                buttonStatus != 0 ? "ORANGE" : "BLUE");
      continue;
    }
    if (buttonStatus == device.lastButtonStatus) {
      continue;
    }

    device.lastButtonStatus = buttonStatus;
    const bool pressed = buttonStatus != 0;
    setKeyLed(port, device, pressed ? colorOrange : colorBlue,
              pressed ? "ORANGE" : "BLUE");
    NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_KEY][%s] id=%u uid=", port.name,
                  device.id);
    printUid(device);
    NANO_VERBOSE_LOGF(" state=%s led=%s\n", pressed ? "PRESSED" : "RELEASED",
                  pressed ? "ORANGE" : "BLUE");
    if (device.uidValid) {
      oscSendChainKey(device.uid, UID_SIZE, pressed);
    }
  }
}

void pollEncoders(ChainPortContext& port) {
  if (!port.connected) return;
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (device.type != CHAIN_ENCODER_TYPE_CODE || !device.uidValid) continue;

    int16_t absoluteValue = 0;
    uint8_t rawButton = 0;
    const chain_status_t valueResult =
        port.bus.getEncoderValue(device.id, &absoluteValue);
    const chain_status_t buttonResult =
        port.bus.getEncoderButtonStatus(device.id, &rawButton);
    if (valueResult != CHAIN_OK || buttonResult != CHAIN_OK) {
      if (!device.encoderReadErrorReported) {
        NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_ENCODER][%s] read_error id=%u value=%s button=%s\n",
                      port.name, device.id, chainStatusName(valueResult),
                      chainStatusName(buttonResult));
        device.encoderReadErrorReported = true;
      }
      continue;
    }
    device.encoderReadErrorReported = false;
    if (!device.encoderInitialized) {
      device.lastEncoderAbsolute = absoluteValue;
      device.encoderInitialized = true;
    } else if (absoluteValue != device.lastEncoderAbsolute) {
      const int16_t delta = absoluteValue - device.lastEncoderAbsolute;
      device.lastEncoderAbsolute = absoluteValue;
      oscSendChainEncoderRotation(device.uid, UID_SIZE, absoluteValue, delta);
    }

    const uint8_t buttonStatus = rawButton != 0 ? 1 : 0;
    if (!device.buttonInitialized) {
      device.lastButtonStatus = buttonStatus;
      device.buttonInitialized = true;
    } else if (buttonStatus != device.lastButtonStatus) {
      device.lastButtonStatus = buttonStatus;
      const bool pressed = buttonStatus != 0;
      const bool sequenceMode =
          oscSendChainEncoderClick(device.uid, UID_SIZE, pressed);
      setDeviceLed(port, device,
                   pressed ? (sequenceMode ? colorGreen : colorRed) : colorBlue,
                   pressed ? (sequenceMode ? "GREEN" : "RED") : "BLUE");
      NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_ENCODER][%s] id=%u click=%s\n",
                    port.name, device.id,
                    pressed ? "PRESSED" : "RELEASED");
    }
  }
}

void pollAngles(ChainPortContext& port) {
  if (!port.connected) return;
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (device.type != CHAIN_ANGLE_TYPE_CODE || !device.uidValid) continue;
    String uid;
    uid.reserve(UID_SIZE * 2);
    for (size_t i = 0; i < UID_SIZE; ++i) {
      char byteText[3];
      snprintf(byteText, sizeof(byteText), "%02X", device.uid[i]);
      uid += byteText;
    }
    AngleSetting* setting = angleSettingsEnsure(
        String("chain:") + uid, String("Chain Angle ") + uid);
    if (!setting) continue;
    int value = -1;
    chain_status_t result = CHAIN_PARAMETER_ERROR;
    if (setting->use12Bit) {
      uint16_t raw = 0;
      result = port.bus.getAngle12BitAdc(device.id, &raw);
      if (result == CHAIN_OK) value = static_cast<int>(raw);
    } else {
      uint8_t raw = 0;
      result = port.bus.getAngle8BitAdc(device.id, &raw);
      if (result == CHAIN_OK) value = static_cast<int>(raw);
    }
    if (result != CHAIN_OK || value < 0) {
      if (!device.angleReadErrorReported) {
        NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_ANGLE][%s] read_error id=%u status=%s\n",
                      port.name, device.id, chainStatusName(result));
        device.angleReadErrorReported = true;
      }
      continue;
    }
    device.angleReadErrorReported = false;
    if (!device.angleInitialized) {
      device.lastAngleValue = value;
      device.angleInitialized = true;
      continue;
    }
    if (abs(value - device.lastAngleValue) >= max(1, setting->deadband)) {
      device.lastAngleValue = value;
      oscSendChainAngle(device.uid, UID_SIZE, value);
    }
  }
}

void pollJoysticks(ChainPortContext& port) {
  if (!port.connected) return;
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (device.type != CHAIN_JOYSTICK_TYPE_CODE || !device.uidValid) continue;
    int8_t x = 0, y = 0; uint8_t rawButton = 0;
    const chain_status_t axesResult = port.bus.getJoystickMappedInt8Value(device.id, &x, &y);
    const chain_status_t buttonResult = port.bus.getJoystickButtonStatus(device.id, &rawButton);
    if (axesResult != CHAIN_OK || buttonResult != CHAIN_OK) {
      if (!device.joystickReadErrorReported) NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN_JOYSTICK][%s] read_error id=%u axes=%s button=%s\n", port.name, device.id, chainStatusName(axesResult), chainStatusName(buttonResult));
      device.joystickReadErrorReported = true; continue;
    }
    device.joystickReadErrorReported = false;
    String uid; for(size_t i=0;i<UID_SIZE;++i){char b[3];snprintf(b,sizeof(b),"%02X",device.uid[i]);uid+=b;}
    JoystickSetting* setting = joystickSettingsEnsure(String("chain:")+uid, String("Chain Joystick ")+uid);
    if (!setting) continue;
    if (!device.joystickInitialized) { device.lastJoystickX=x; device.lastJoystickY=y; device.joystickInitialized=true; }
    else {
      const bool cx=abs((int)x-(int)device.lastJoystickX)>=max(1,setting->deadband);
      const bool cy=abs((int)y-(int)device.lastJoystickY)>=max(1,setting->deadband);
      if(cx||cy){device.lastJoystickX=x;device.lastJoystickY=y;oscSendChainJoystickAxes(device.uid,UID_SIZE,x,y,port.portMask,cx,cy);}
    }
    const uint8_t button=rawButton?1:0;
    if(!device.buttonInitialized){device.lastButtonStatus=button;device.buttonInitialized=true;}
    else if(button!=device.lastButtonStatus){device.lastButtonStatus=button;const bool pressed=button!=0;const bool seq=oscSendChainJoystickClick(device.uid,UID_SIZE,pressed);setDeviceLed(port,device,pressed?(seq?colorGreen:colorRed):colorBlue,pressed?(seq?"GREEN":"RED"):"BLUE");}
  }
}

void pollTofs(ChainPortContext& port) {
  if (!port.connected) return;
  const unsigned long now = millis();
  for (uint16_t index = 0; index < port.deviceCount; ++index) {
    DeviceSnapshot& device = port.devices[index];
    if (device.type != CHAIN_TOF_TYPE_CODE || !device.uidValid) continue;
    String uid;
    for (size_t i = 0; i < UID_SIZE; ++i) { char b[3]; snprintf(b, sizeof(b), "%02X", device.uid[i]); uid += b; }
    TofSetting* setting = tofSettingsEnsure(String("chain:") + uid,
                                             String("Chain ToF ") + uid);
    if (!setting) continue;
    if (!device.tofConfigured) {
      if (device.lastTofConfigMs && now - device.lastTofConfigMs < 2000) continue;
      device.lastTofConfigMs = now; uint8_t status = 0;
      chain_status_t result = port.bus.setToFMeasureMode(
          device.id, CHAIN_TOF_MODE_CONTINUOUS, &status);
      if (result != CHAIN_OK || status == 0) continue;
      status = 0; result = port.bus.setToFMeasureTime(device.id, 50, &status);
      if (result != CHAIN_OK || status == 0) continue;
      device.tofConfigured = true; device.tofInitialized = false;
    }
    if (device.lastTofPollMs && now - device.lastTofPollMs < 50) continue;
    device.lastTofPollMs = now; uint16_t mm = 0;
    if (port.bus.getToFDistance(device.id, &mm, 30) != CHAIN_OK) {
      if (++device.tofReadFailures >= 5) {
        device.tofReadFailures = 0; device.tofConfigured = false;
        device.tofInitialized = false;
      }
      continue;
    }
    device.tofReadFailures = 0;
    if (mm < 30 || mm >= setting->maxDistanceMm) {
      device.tofInitialized = false; device.lastTofMm = -1; continue;
    }
    const int value = static_cast<int>(mm);
    if (!device.tofInitialized) device.tofInitialized = true;
    else if (abs(value - device.lastTofMm) < max(1, setting->deadband)) continue;
    device.lastTofMm = value;
    oscSendChainTof(device.uid, UID_SIZE, value);
  }
}

void scanChainPort(ChainPortContext& port) {
  const bool connected = port.bus.isDeviceConnected(1, 20);
  if (!connected) {
    oscBeginChainPortUpdate(port.portMask);
    if (port.connected || port.firstScan) {
      NANO_VERBOSE_LOGF(
          "[ChainOSCnano][CHAIN][%s] state=DISCONNECTED devices=0\n",
          port.name);
    }
    port.connected = false;
    port.deviceCount = 0;
    drainAllKeyReports(port);
    port.firstScan = false;
    return;
  }

  uint16_t reportedCount = 0;
  const chain_status_t countStatus =
      port.bus.getDeviceNum(&reportedCount, 150);
  if (countStatus != CHAIN_OK) {
    NANO_VERBOSE_LOGF(
        "[ChainOSCnano][CHAIN][%s] scan_error=get_count status=%d(%s) "
        "previous_state_retained=true\n",
        port.name, static_cast<int>(countStatus),
        chainStatusName(countStatus));
    return;
  }

  if (reportedCount > CHAIN_MAX_DEVICES) {
    NANO_VERBOSE_LOGF(
        "[ChainOSCnano][CHAIN][%s] scan_error=too_many reported=%u max=%u\n",
        port.name, reportedCount, CHAIN_MAX_DEVICES);
    return;
  }

  device_info_t deviceInfo[CHAIN_MAX_DEVICES] = {};
  device_list_t list = {reportedCount, deviceInfo};
  if (reportedCount > 0 && !port.bus.getDeviceList(&list, 150)) {
    NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN][%s] scan_error=get_list\n",
                  port.name);
    return;
  }

  DeviceSnapshot currentDevices[CHAIN_MAX_DEVICES] = {};
  bool uidError = false;
  for (uint16_t index = 0; index < reportedCount; ++index) {
    currentDevices[index].id = deviceInfo[index].id;
    currentDevices[index].type = deviceInfo[index].device_type;

    uint8_t operationStatus = 0;
    const chain_status_t uidStatus = port.bus.getUID(
        currentDevices[index].id, UID_TYPE_12_BYTE,
        currentDevices[index].uid, UID_SIZE, &operationStatus, 150);
    currentDevices[index].uidValid =
        uidStatus == CHAIN_OK && operationStatus != 0;
    if (!currentDevices[index].uidValid) {
      NANO_VERBOSE_LOGF(
          "[ChainOSCnano][CHAIN][%s] scan_error=get_uid id=%u "
          "status=%d(%s) operation=%u previous_state_retained=true\n",
          port.name, currentDevices[index].id, static_cast<int>(uidStatus),
          chainStatusName(uidStatus), operationStatus);
      uidError = true;
    }
  }
  if (uidError) {
    return;
  }

  oscBeginChainPortUpdate(port.portMask);
  for (uint16_t index = 0; index < reportedCount; ++index) {
    if (currentDevices[index].type == CHAIN_KEY_TYPE_CODE &&
        currentDevices[index].uidValid) {
      oscRegisterChainKey(currentDevices[index].uid, UID_SIZE,
                          port.portMask);
    } else if (currentDevices[index].type == CHAIN_ENCODER_TYPE_CODE &&
               currentDevices[index].uidValid) {
      oscRegisterChainEncoder(currentDevices[index].uid, UID_SIZE,
                              port.portMask);
    } else if (currentDevices[index].type == CHAIN_ANGLE_TYPE_CODE &&
               currentDevices[index].uidValid) {
      oscRegisterChainAngle(currentDevices[index].uid, UID_SIZE,
                            port.portMask);
    } else if (currentDevices[index].type == CHAIN_TOF_TYPE_CODE &&
               currentDevices[index].uidValid) {
      oscRegisterChainTof(currentDevices[index].uid, UID_SIZE, port.portMask);
    } else if (currentDevices[index].type == CHAIN_JOYSTICK_TYPE_CODE &&
               currentDevices[index].uidValid) {
      oscRegisterChainJoystick(currentDevices[index].uid, UID_SIZE, port.portMask);
    }
  }

  const bool changed = snapshotChanged(port, currentDevices, reportedCount);
  if (changed) {
    NANO_VERBOSE_LOGF("[ChainOSCnano][CHAIN][%s] state=%s\n", port.name,
                  port.connected ? "CHANGED" : "CONNECTED");
    printSnapshot(port, currentDevices, reportedCount);
    drainAllKeyReports(port);
    saveSnapshot(port, currentDevices, reportedCount);
    port.connected = true;
    initializeKeys(port);
    initializeEncoders(port);
    initializeAngles(port);
    initializeTofs(port);
    initializeJoysticks(port);
  }
  port.connected = true;
  port.firstScan = false;
}

void setupPort(ChainPortContext& port) {
  port.bus.begin(port.serial, CHAIN_BAUD, port.rxPin, port.txPin);
  NANO_VERBOSE_LOGF(
      "[ChainOSCnano][CHAIN][%s] RX=%u TX=%u baud=%lu enabled=true\n",
      port.name, port.rxPin, port.txPin,
      static_cast<unsigned long>(CHAIN_BAUD));
  port.lastScanMs = millis();
  port.lastKeyPollMs = millis();
}

void updatePort(ChainPortContext& port, unsigned long now) {
  if (port.firstScan && now < BOOT_DIAGNOSTICS_DELAY_MS) {
    return;
  }
  if (now - port.lastScanMs >= CHAIN_SCAN_INTERVAL_MS) {
    port.lastScanMs = now;
    scanChainPort(port);
  }
  if (now - port.lastKeyPollMs >= CHAIN_KEY_POLL_INTERVAL_MS) {
    port.lastKeyPollMs = now;
    updateIdentifyLeds(port);
    pollKeys(port);
    pollEncoders(port);
    pollAngles(port);
    pollTofs(port);
    pollJoysticks(port);
  }
}

}  // namespace

void chainProbeSetup() {
  setupPort(portG1G2);
  NANO_VERBOSE_PRINTLN("[ChainOSCnano][CHAIN] single_port=true");
}

void chainProbeUpdate() {
  const unsigned long now = millis();
  updatePort(portG1G2, now);
}

bool chainProbeIdentifyDevice(const String& identity) {
  return identifyOnPort(portG1G2, identity);
}

size_t chainProbeConnectedDeviceCount() {
  return static_cast<size_t>(portG1G2.deviceCount);
}

bool chainProbeConnectedDeviceAt(size_t index, String& identity,
                                uint8_t& deviceType) {
  const ChainPortContext* port = &portG1G2;
  if (index >= port->deviceCount) return false;

  const DeviceSnapshot& device = port->devices[index];
  if (!device.uidValid) return false;

  identity = F("chain:");
  identity.reserve(6 + UID_SIZE * 2);
  for (size_t byteIndex = 0; byteIndex < UID_SIZE; ++byteIndex) {
    char byteText[3];
    snprintf(byteText, sizeof(byteText), "%02X", device.uid[byteIndex]);
    identity += byteText;
  }
  deviceType = static_cast<uint8_t>(device.type);
  return true;
}
