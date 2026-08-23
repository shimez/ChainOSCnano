#pragma once

#include <Arduino.h>

#include "angle_settings.h"
#include "encoder_settings.h"
#include "joystick_settings.h"
#include "key_settings.h"
#include "tof_settings.h"

// Compact C1 storage: one NVS string per device. Existing per-field formats
// remain readable in each settings module and are migrated on the next save.
String compactStorageNamespace(const String& identity);
void compactStorageDelete(const String& identity);
bool compactStorageLoad(const String& nameSpace, KeySetting& setting);
bool compactStorageLoad(const String& nameSpace, EncoderSetting& setting);
bool compactStorageLoad(const String& nameSpace, AngleSetting& setting);
bool compactStorageLoad(const String& nameSpace, TofSetting& setting);
bool compactStorageLoad(const String& nameSpace, JoystickSetting& setting);

bool compactStorageSave(const String& nameSpace, const KeySetting& setting);
bool compactStorageSave(const String& nameSpace, const EncoderSetting& setting);
bool compactStorageSave(const String& nameSpace, const AngleSetting& setting);
bool compactStorageSave(const String& nameSpace, const TofSetting& setting);
bool compactStorageSave(const String& nameSpace, const JoystickSetting& setting);
