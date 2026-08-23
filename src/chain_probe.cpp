#include "chain_probe.h"

#include <Arduino.h>
#include <M5Chain.h>
#include <string.h>

#include "config.h"

namespace {

constexpr size_t UID_SIZE = 12;

struct DeviceSnapshot {
  uint16_t id;
  chain_device_type_t type;
  uint8_t uid[UID_SIZE];
};

HardwareSerial chainSerial(1);
Chain chainBus;
DeviceSnapshot previousDevices[CHAIN_MAX_DEVICES] = {};
uint16_t previousCount = 0;
bool firstScan = true;
unsigned long lastScanMs = 0;

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
    previousCount = 0;
    firstScan = false;
    return;
  }

  uint16_t reportedCount = 0;
  const chain_status_t countStatus = chainBus.getDeviceNum(&reportedCount, 150);
  if (countStatus != CHAIN_OK) {
    Serial.printf("[ChainOSCnano][CHAIN] scan_error=get_count status=%d(%s)\n",
                  static_cast<int>(countStatus), statusName(countStatus));
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
  }
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
}

void chainProbeUpdate() {
  if (!CHAIN_UART_ENABLED) return;
  const unsigned long now = millis();
  if (now < BOOT_DIAGNOSTICS_DELAY_MS) return;
  if (now - lastScanMs < CHAIN_SCAN_INTERVAL_MS) return;
  lastScanMs = now;
  scanBus();
}

