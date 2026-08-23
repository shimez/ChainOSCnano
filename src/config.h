#pragma once

#include <Arduino.h>

static constexpr const char* APP_NAME = "ChainOSCnano";
static constexpr const char* APP_VERSION = "0.1.0";
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
static constexpr uint16_t CHAIN_MAX_DEVICES = 16;

