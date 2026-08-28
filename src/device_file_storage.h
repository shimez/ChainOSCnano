#pragma once

#include <Arduino.h>

#include "angle_settings.h"
#include "encoder_settings.h"
#include "joystick_settings.h"
#include "key_settings.h"
#include "tof_settings.h"

enum class DeviceFileLoadResult : uint8_t {
  NotFound,
  Loaded,
  Error,
};

bool deviceFileStorageBegin();
void deviceFileStorageLogUsage(const char* phase);

DeviceFileLoadResult deviceFileStorageLoad(KeySetting& setting);
DeviceFileLoadResult deviceFileStorageLoad(EncoderSetting& setting);
DeviceFileLoadResult deviceFileStorageLoad(AngleSetting& setting);
DeviceFileLoadResult deviceFileStorageLoad(TofSetting& setting);
DeviceFileLoadResult deviceFileStorageLoad(JoystickSetting& setting);

bool deviceFileStorageSave(const KeySetting& setting);
bool deviceFileStorageSave(const EncoderSetting& setting);
bool deviceFileStorageSave(const AngleSetting& setting);
bool deviceFileStorageSave(const TofSetting& setting);
bool deviceFileStorageSave(const JoystickSetting& setting);

bool deviceFileStorageRemove(const char* type, const String& identity);
size_t deviceFileStorageList(const char* type, String* identities,
                             size_t capacity);

