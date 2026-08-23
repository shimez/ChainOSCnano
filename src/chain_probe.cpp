#include "chain_probe.h"

#include <Arduino.h>
#include <M5Chain.h>
#include <string.h>

#include "config.h"
#include "osc_manager.h"
#include "key_settings.h"

namespace {

constexpr size_t UID_SIZE = 12;

struct DeviceSnapshot {
  uint16_t id;
  chain_device_type_t type;
  uint8_t uid[UID_SIZE];
  uint8_t lastButtonStatus;
  bool buttonInitialized;
  bool keyReadErrorReported;
};

HardwareSerial chainSerial(1);
Chain chainBus;
DeviceSnapshot previousDevices[CHAIN_MAX_DEVICES] = {};
uint16_t previousCount = 0;
bool firstScan = true;
unsigned long lastScanMs = 0;
unsigned long lastKeyPollMs = 0;
unsigned long lastBusRecoveryMs = 0;
uint8_t consecutiveScanFailures = 0;
uint8_t colorBlue[] = {0, 0, 255};
uint8_t colorOrange[] = {255, 64, 0};
uint8_t colorGreen[] = {0, 255, 64};

bool hasStatusLed(chain_device_type_t type) {
  return type == CHAIN_KEY_TYPE_CODE || type == CHAIN_ENCODER_TYPE_CODE ||
         type == CHAIN_ANGLE_TYPE_CODE || type == CHAIN_JOYSTICK_TYPE_CODE ||
         type == CHAIN_TOF_TYPE_CODE;
}

const char* statusName(chain_status_t status) {
  switch (status) {
    case CHAIN_OK: return "OK";
    case CHAIN_PARAMETER_ERROR: return "PARAMETER_ERROR";
    case CHAIN_RETURN_PACKET_ERROR: return "RETURN_PACKET_ERROR";
    case CHAIN_BUSY: return "BUSY";
    case CHAIN_TIMEOUT: return "TIMEOUT";
    default: return "UNKNOWN";
  }
}

const char* typeName(chain_device_type_t type) {
  switch (type) {
    case CHAIN_ENCODER_TYPE_CODE: return "Encoder";
    case CHAIN_ANGLE_TYPE_CODE: return "Angle";
    case CHAIN_KEY_TYPE_CODE: return "Key";
    case CHAIN_JOYSTICK_TYPE_CODE: return "Joystick";
    case CHAIN_TOF_TYPE_CODE: return "ToF";
    default: return "Unknown";
  }
}

void printUid(const uint8_t* uid) {
  for (size_t index = 0; index < UID_SIZE; ++index) {
    Serial.printf("%02X", uid[index]);
  }
}

String uidString(const uint8_t* uid) {
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  String value;
  value.reserve(UID_SIZE * 2);
  for (size_t index = 0; index < UID_SIZE; ++index) {
    value += HEX_DIGITS[(uid[index] >> 4) & 0x0F];
    value += HEX_DIGITS[uid[index] & 0x0F];
  }
  return value;
}

bool sequenceMode(const DeviceSnapshot& device) {
  KeySetting* setting = keySettingsFind(uidString(device.uid));
  return setting && setting->mode == MODE_SEQUENCE;
}

void drainKeyReports(uint16_t id) {
  chain_button_press_type_t ignoredType;
  while (chainBus.getKeyButtonPressStatus(id, &ignoredType)) {
    // Raw state is authoritative. Drain queued reports because M5Chain stores
    // each report in a dynamically allocated linked-list node.
  }
}

void drainAllKeyReports() {
  for (uint16_t id = 1; id <= CHAIN_MAX_DEVICES; ++id) {
    drainKeyReports(id);
  }
}

void recordScanSuccess() {
  consecutiveScanFailures = 0;
}

void recordScanFailure(const char* reason) {
  if (consecutiveScanFailures < 255) ++consecutiveScanFailures;
  const unsigned long now = millis();
  if (consecutiveScanFailures < CHAIN_BUS_RECOVERY_FAILURES ||
      now - lastBusRecoveryMs < CHAIN_BUS_RECOVERY_INTERVAL_MS) {
    return;
  }

  drainAllKeyReports();
  chainSerial.end();
  delay(2);
  chainBus.begin(&chainSerial, CHAIN_BAUD, CHAIN_RX_PIN, CHAIN_TX_PIN);
  lastBusRecoveryMs = millis();
  lastKeyPollMs = lastBusRecoveryMs;
  consecutiveScanFailures = 0;
  Serial.printf(
      "[ChainOSCnano][CHAIN] bus_reinitialized reason=%s RX=%u TX=%u\n",
      reason, CHAIN_RX_PIN, CHAIN_TX_PIN);
}

bool setDeviceLed(DeviceSnapshot& device, uint8_t* color,
                  const char* colorName) {
  uint8_t operationStatus = 0;
  const chain_status_t status = chainBus.setRGBValue(
      device.id, 0, 1, color, 3, &operationStatus, 100);
  if (status == CHAIN_OK && operationStatus != 0) return true;
  Serial.printf(
      "[ChainOSCnano][CHAIN_KEY] led_error id=%u color=%s "
      "status=%d(%s) operation=%u\n",
      device.id, colorName, static_cast<int>(status), statusName(status),
      operationStatus);
  return false;
}

bool deviceIdentityMatches(const DeviceSnapshot& device) {
  uint8_t currentUid[UID_SIZE] = {};
  uint8_t operationStatus = 0;
  const chain_status_t status = chainBus.getUID(
      device.id, UID_TYPE_12_BYTE, currentUid, UID_SIZE, &operationStatus, 50);
  if (status == CHAIN_OK && operationStatus != 0 &&
      memcmp(currentUid, device.uid, UID_SIZE) == 0) {
    return true;
  }

  Serial.printf(
      "[ChainOSCnano][CHAIN_KEY] identity_check_failed id=%u "
      "status=%d(%s) operation=%u expected_uid=",
      device.id, static_cast<int>(status), statusName(status), operationStatus);
  printUid(device.uid);
  Serial.print(" current_uid=");
  if (status == CHAIN_OK && operationStatus != 0) {
    printUid(currentUid);
  } else {
    Serial.print("unavailable");
  }
  Serial.println(" event_discarded=true");
  return false;
}

void initializeDeviceLed(DeviceSnapshot& device) {
  uint8_t operationStatus = 0;
  const chain_status_t status = chainBus.setRGBLight(
      device.id, CHAIN_DEVICE_LED_BRIGHTNESS, &operationStatus,
      CHAIN_SAVE_FLASH_DISABLE, 100);
  if (status != CHAIN_OK || operationStatus == 0) {
    Serial.printf(
        "[ChainOSCnano][CHAIN] brightness_error id=%u status=%d(%s) "
        "operation=%u\n",
        device.id, static_cast<int>(status), statusName(status),
        operationStatus);
  }
  setDeviceLed(device, colorBlue, "BLUE");
}

void initializeKey(DeviceSnapshot& device) {
  uint8_t rawStatus = 0;
  const chain_status_t status =
      chainBus.getKeyButtonStatus(device.id, &rawStatus, 100);
  drainKeyReports(device.id);
  if (status != CHAIN_OK) {
    Serial.printf(
        "[ChainOSCnano][CHAIN_KEY] init_error id=%u status=%d(%s)\n",
        device.id, static_cast<int>(status), statusName(status));
    device.buttonInitialized = false;
    return;
  }

  device.lastButtonStatus = rawStatus != 0 ? 1 : 0;
  device.buttonInitialized = true;
  device.keyReadErrorReported = false;
  const bool sequence = sequenceMode(device);
  const bool ledUpdated = setDeviceLed(
      device, device.lastButtonStatus != 0
                  ? (sequence ? colorGreen : colorOrange)
                  : colorBlue,
      device.lastButtonStatus != 0
          ? (sequence ? "GREEN" : "ORANGE")
          : "BLUE");
  if (!ledUpdated) device.buttonInitialized = false;

  Serial.printf("[ChainOSCnano][CHAIN_KEY] ready id=%u uid=", device.id);
  printUid(device.uid);
  Serial.printf(
      " initial=%s led=%s\n",
      device.lastButtonStatus != 0 ? "PRESSED" : "RELEASED",
      ledUpdated ? (device.lastButtonStatus != 0
                         ? (sequence ? "GREEN" : "ORANGE")
                         : "BLUE")
                 : "ERROR");
}

void initializeDevices() {
  for (uint16_t index = 0; index < previousCount; ++index) {
    if (hasStatusLed(previousDevices[index].type)) {
      initializeDeviceLed(previousDevices[index]);
    }
    if (previousDevices[index].type == CHAIN_KEY_TYPE_CODE) {
      initializeKey(previousDevices[index]);
    }
  }
}

bool pollKeys() {
  for (uint16_t index = 0; index < previousCount; ++index) {
    DeviceSnapshot& device = previousDevices[index];
    if (device.type != CHAIN_KEY_TYPE_CODE) continue;

    uint8_t rawStatus = 0;
    const chain_status_t status =
        chainBus.getKeyButtonStatus(device.id, &rawStatus, 50);
    drainKeyReports(device.id);
    if (status != CHAIN_OK) {
      if (!device.keyReadErrorReported) {
        Serial.printf(
            "[ChainOSCnano][CHAIN_KEY] read_error id=%u status=%d(%s)\n",
            device.id, static_cast<int>(status), statusName(status));
        device.keyReadErrorReported = true;
      }
      continue;
    }

    device.keyReadErrorReported = false;
    const uint8_t buttonStatus = rawStatus != 0 ? 1 : 0;
    if (!device.buttonInitialized) {
      device.lastButtonStatus = buttonStatus;
      device.buttonInitialized = true;
      const bool sequence = sequenceMode(device);
      if (!deviceIdentityMatches(device) ||
          !setDeviceLed(device,
                        buttonStatus != 0
                            ? (sequence ? colorGreen : colorOrange)
                            : colorBlue,
                        buttonStatus != 0
                            ? (sequence ? "GREEN" : "ORANGE")
                            : "BLUE")) {
        device.buttonInitialized = false;
        return true;
      }
      continue;
    }
    if (buttonStatus == device.lastButtonStatus) continue;

    const bool pressed = buttonStatus != 0;
    if (!deviceIdentityMatches(device)) return true;
    const bool sequence = sequenceMode(device);
    if (!setDeviceLed(device,
                      pressed ? (sequence ? colorGreen : colorOrange)
                              : colorBlue,
                      pressed ? (sequence ? "GREEN" : "ORANGE") : "BLUE")) {
      Serial.printf(
          "[ChainOSCnano][CHAIN_KEY] event_discarded id=%u "
          "reason=led_update_failed\n",
          device.id);
      return true;
    }

    device.lastButtonStatus = buttonStatus;
    Serial.printf("[ChainOSCnano][CHAIN_KEY] id=%u uid=", device.id);
    printUid(device.uid);
    Serial.printf(" state=%s led=%s\n", pressed ? "PRESSED" : "RELEASED",
                  pressed ? (sequence ? "GREEN" : "ORANGE") : "BLUE");
    oscSendKeyEvent(device.uid, UID_SIZE, pressed);
  }
  return false;
}

bool sameSnapshot(const DeviceSnapshot* devices, uint16_t count) {
  if (count != previousCount) return false;
  for (uint16_t index = 0; index < count; ++index) {
    if (devices[index].id != previousDevices[index].id ||
        devices[index].type != previousDevices[index].type ||
        memcmp(devices[index].uid, previousDevices[index].uid, UID_SIZE) != 0) {
      return false;
    }
  }
  return true;
}

void saveSnapshot(const DeviceSnapshot* devices, uint16_t count) {
  previousCount = count;
  for (uint16_t index = 0; index < count; ++index) {
    previousDevices[index] = devices[index];
  }
}

void scanBus() {
  const bool connected = chainBus.isDeviceConnected(1, 20);
  if (!connected) {
    if (firstScan || previousCount != 0) {
      Serial.println("[ChainOSCnano][CHAIN] state=DISCONNECTED devices=0");
    }
    drainAllKeyReports();
    previousCount = 0;
    firstScan = false;
    recordScanFailure("disconnected");
    return;
  }

  uint16_t reportedCount = 0;
  const chain_status_t countStatus = chainBus.getDeviceNum(&reportedCount, 150);
  if (countStatus != CHAIN_OK) {
    Serial.printf("[ChainOSCnano][CHAIN] scan_error=get_count status=%d(%s)\n",
                  static_cast<int>(countStatus), statusName(countStatus));
    recordScanFailure("get_count");
    return;
  }
  if (reportedCount > CHAIN_MAX_DEVICES) {
    Serial.printf("[ChainOSCnano][CHAIN] scan_error=too_many reported=%u max=%u\n",
                  reportedCount, CHAIN_MAX_DEVICES);
    return;
  }

  device_info_t deviceInfo[CHAIN_MAX_DEVICES] = {};
  device_list_t list = {reportedCount, deviceInfo};
  if (reportedCount > 0 && !chainBus.getDeviceList(&list, 150)) {
    Serial.println("[ChainOSCnano][CHAIN] scan_error=get_list");
    recordScanFailure("get_list");
    return;
  }

  DeviceSnapshot current[CHAIN_MAX_DEVICES] = {};
  for (uint16_t index = 0; index < reportedCount; ++index) {
    current[index].id = deviceInfo[index].id;
    current[index].type = deviceInfo[index].device_type;
    uint8_t operationStatus = 0;
    const chain_status_t uidStatus = chainBus.getUID(
        current[index].id, UID_TYPE_12_BYTE, current[index].uid, UID_SIZE,
        &operationStatus, 150);
    if (uidStatus != CHAIN_OK || operationStatus == 0) {
      Serial.printf(
          "[ChainOSCnano][CHAIN] scan_error=get_uid id=%u status=%d(%s) operation=%u\n",
          current[index].id, static_cast<int>(uidStatus), statusName(uidStatus),
          operationStatus);
      recordScanFailure("get_uid");
      return;
    }
  }

  if (!sameSnapshot(current, reportedCount)) {
    Serial.printf("[ChainOSCnano][CHAIN] state=%s devices=%u\n",
                  previousCount == 0 ? "CONNECTED" : "CHANGED", reportedCount);
    for (uint16_t index = 0; index < reportedCount; ++index) {
      Serial.printf("[ChainOSCnano][CHAIN] index=%u id=%u type=%u(%s) uid=",
                    index, current[index].id,
                    static_cast<unsigned int>(current[index].type),
                    typeName(current[index].type));
      printUid(current[index].uid);
      Serial.println();
    }
    saveSnapshot(current, reportedCount);
    for (uint16_t index = 0; index < previousCount; ++index) {
      if (previousDevices[index].type == CHAIN_KEY_TYPE_CODE) {
        keySettingsEnsure(uidString(previousDevices[index].uid));
      }
    }
    initializeDevices();
  }
  recordScanSuccess();
  firstScan = false;
}

}  // namespace

void chainProbeSetup() {
  if (!CHAIN_UART_ENABLED) {
    Serial.println("[ChainOSCnano][CHAIN] uart=disabled");
    return;
  }
  chainBus.begin(&chainSerial, CHAIN_BAUD, CHAIN_RX_PIN, CHAIN_TX_PIN);
  Serial.printf("[ChainOSCnano][CHAIN] RX=%u TX=%u baud=%lu enabled=true\n",
                CHAIN_RX_PIN, CHAIN_TX_PIN,
                static_cast<unsigned long>(CHAIN_BAUD));
  lastScanMs = millis();
  lastKeyPollMs = lastScanMs;
  lastBusRecoveryMs = lastScanMs;
}

void chainProbeUpdate() {
  if (!CHAIN_UART_ENABLED) return;
  const unsigned long now = millis();
  if (now < BOOT_DIAGNOSTICS_DELAY_MS) return;
  if (now - lastScanMs >= CHAIN_SCAN_INTERVAL_MS) {
    lastScanMs = now;
    scanBus();
  }
  if (now - lastKeyPollMs >= CHAIN_KEY_POLL_INTERVAL_MS) {
    lastKeyPollMs = now;
    if (pollKeys()) {
      lastScanMs = now;
      scanBus();
    }
  }
}

size_t chainProbeKeyCount() {
  size_t count = 0;
  for (uint16_t index = 0; index < previousCount; ++index)
    if (previousDevices[index].type == CHAIN_KEY_TYPE_CODE) ++count;
  return count;
}

String chainProbeKeyUid(size_t requestedIndex) {
  size_t keyIndex = 0;
  for (uint16_t index = 0; index < previousCount; ++index) {
    if (previousDevices[index].type != CHAIN_KEY_TYPE_CODE) continue;
    if (keyIndex++ == requestedIndex) return uidString(previousDevices[index].uid);
  }
  return String();
}

bool chainProbeKeyConnected(const String& uid) {
  for (uint16_t index = 0; index < previousCount; ++index) {
    if (previousDevices[index].type == CHAIN_KEY_TYPE_CODE &&
        uidString(previousDevices[index].uid) == uid) return true;
  }
  return false;
}
