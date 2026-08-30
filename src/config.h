#pragma once

#include <stdint.h>

static constexpr const char* APP_NAME = "ChainOSCnano";
static constexpr const char* APP_VERSION = "1.0.0";
static constexpr unsigned long SERIAL_BAUD = 115200;
static constexpr unsigned long BOOT_DIAGNOSTICS_DELAY_MS = 5000;
static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 5000;

#define CHAINOSCNANO_WEB_PERF_DEBUG 0
#define CHAINOSCNANO_STORAGE_DEBUG 1

static constexpr bool HARDWARE_GPIO_ENABLED = true;
static constexpr uint8_t LED_DATA_PIN = 20;
static constexpr uint8_t LED_POWER_PIN = 19;
static constexpr uint8_t LED_COUNT = 1;
static constexpr bool CHAIN_POWER_CONTROL_ENABLED = true;
static constexpr uint8_t CHAIN_POWER_PIN = LED_POWER_PIN;
static constexpr uint8_t CHAIN_POWER_ACTIVE_LEVEL = 1;
static constexpr bool RGB_LED_ENABLED = true;
static constexpr uint8_t RGB_LED_PIN = LED_DATA_PIN;
static constexpr uint8_t RGB_LED_COUNT = LED_COUNT;
static constexpr uint8_t RGB_LED_BRIGHTNESS = 24;
static constexpr uint8_t BUILT_IN_BUTTON_PIN = 9;
static constexpr unsigned long BUTTON_DEBOUNCE_MS = 20;

// M5NanoC6 Grove/Chain UART.
static constexpr uint8_t CHAIN_G1_G2_RX_PIN = 1;
static constexpr uint8_t CHAIN_G1_G2_TX_PIN = 2;
static constexpr bool CHAIN_UART_ENABLED = true;
static constexpr uint8_t CHAIN_RX_PIN = CHAIN_G1_G2_RX_PIN;
static constexpr uint8_t CHAIN_TX_PIN = CHAIN_G1_G2_TX_PIN;
static constexpr uint32_t CHAIN_BAUD = 115200;
static constexpr unsigned long CHAIN_SCAN_INTERVAL_MS = 2000;
static constexpr uint16_t CHAIN_MAX_DEVICES = 16;
static constexpr unsigned long CHAIN_KEY_POLL_INTERVAL_MS = 25;
static constexpr uint8_t CHAIN_KEY_LED_BRIGHTNESS = 60;

static constexpr const char* WIFI_AP_SSID = "ChainOSCnano-Setup";
static constexpr const char* WIFI_AP_PASSWORD = "12345678";
static constexpr const char* WIFI_MDNS_HOST = "chainoscnano";
static constexpr const char* WIFI_PREFS_NAMESPACE = "chainoscnano";
static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr unsigned long NETWORK_RESTART_DELAY_MS = 1200;
static constexpr uint8_t CAPTIVE_DNS_PORT = 53;
static constexpr int8_t WIFI_TX_POWER_QDBM = 80;
