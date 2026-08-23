#pragma once

#include <Arduino.h>

static constexpr const char* APP_NAME = "ChainOSCnano";
static constexpr const char* APP_VERSION = "0.4.0";
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr unsigned long BOOT_DIAGNOSTICS_DELAY_MS = 5000;
static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 5000;

// M5NanoC6 pin labels shown on the enclosure.
static constexpr uint8_t CHAIN_RX_PIN = 1;
static constexpr uint8_t CHAIN_TX_PIN = 2;
static constexpr uint32_t CHAIN_BAUD = 115200;
static constexpr uint8_t CHAIN_POWER_PIN = 19;
static constexpr uint8_t CHAIN_POWER_ACTIVE_LEVEL = HIGH;
static constexpr uint8_t RGB_LED_PIN = 20;
static constexpr uint16_t RGB_LED_COUNT = 1;
static constexpr uint8_t RGB_LED_BRIGHTNESS = 24;

// Set false before changing wiring or when diagnosing power behavior.
static constexpr bool CHAIN_POWER_CONTROL_ENABLED = true;
static constexpr bool CHAIN_UART_ENABLED = true;
static constexpr bool RGB_LED_ENABLED = true;

static constexpr unsigned long CHAIN_SCAN_INTERVAL_MS = 2000;
static constexpr unsigned long CHAIN_KEY_POLL_INTERVAL_MS = 25;
static constexpr unsigned long CHAIN_BUS_RECOVERY_INTERVAL_MS = 5000;
static constexpr uint8_t CHAIN_BUS_RECOVERY_FAILURES = 2;
static constexpr uint8_t CHAIN_DEVICE_LED_BRIGHTNESS = 60;
static constexpr uint16_t CHAIN_MAX_DEVICES = 16;

// Wi-Fi provisioning and local status page.
static constexpr const char* WIFI_AP_SSID = "ChainOSCnano-Setup";
static constexpr const char* WIFI_AP_PASSWORD = "12345678";
static constexpr const char* WIFI_MDNS_HOST = "chainoscnano";
static constexpr const char* WIFI_PREFS_NAMESPACE = "chainoscnano";
static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr unsigned long WIFI_CONNECT_LED_BLINK_MS = 500;
static constexpr unsigned long NETWORK_RESTART_DELAY_MS = 1200;
static constexpr uint8_t CAPTIVE_DNS_PORT = 53;

// OSC target used until the user saves a different destination.
static constexpr const char* OSC_DEFAULT_HOST = "192.168.1.100";
static constexpr uint16_t OSC_DEFAULT_PORT = 9000;
