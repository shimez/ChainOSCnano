#pragma once

#include <Arduino.h>

bool systemSettingsSetup();

const String& systemSettingsWifiSsid();
const String& systemSettingsWifiPassword();
const String& systemSettingsOscHost();
uint16_t systemSettingsOscPort();
bool systemSettingsHasUiLanguage();
uint8_t systemSettingsUiLanguage();

bool systemSettingsSaveWifi(const String& ssid, const String& password);
bool systemSettingsClearWifi();
bool systemSettingsSaveOsc(const String& host, uint16_t port);
bool systemSettingsSaveUiLanguage(uint8_t language);
