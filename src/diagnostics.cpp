#include "diagnostics.h"

#include <Arduino.h>
#include <Esp.h>
#include <esp_system.h>

#include "config.h"

namespace {

const char* resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "other-watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_UNKNOWN:
    default: return "unknown";
  }
}

void printBytes(const char* label, uint32_t bytes) {
  Serial.printf("[ChainOSCnano][BOOT] %s=%lu bytes\n", label,
                static_cast<unsigned long>(bytes));
}

}  // namespace

void printBootDiagnostics() {
  Serial.println();
  Serial.println("========================================");
  Serial.printf("%s v%s\n", APP_NAME, APP_VERSION);
  Serial.println("M5NanoC6 bring-up firmware");
  Serial.println("========================================");
  Serial.printf("[ChainOSCnano][BOOT] build=%s %s\n", __DATE__, __TIME__);
  Serial.printf("[ChainOSCnano][BOOT] chip=%s revision=%u cores=%u\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("[ChainOSCnano][BOOT] cpu=%u MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("[ChainOSCnano][BOOT] reset=%s (%d)\n",
                resetReasonText(esp_reset_reason()),
                static_cast<int>(esp_reset_reason()));
  printBytes("flash", ESP.getFlashChipSize());
  printBytes("sketch", ESP.getSketchSize());
  printBytes("free_sketch", ESP.getFreeSketchSpace());
  printBytes("heap", ESP.getHeapSize());
  printBytes("free_heap", ESP.getFreeHeap());
  printBytes("psram", ESP.getPsramSize());
  printBytes("free_psram", ESP.getFreePsram());
  Serial.printf(
      "[ChainOSCnano][BOOT] chain_power=GPIO%u enabled=%s active=%s\n",
      CHAIN_POWER_PIN, CHAIN_POWER_CONTROL_ENABLED ? "true" : "false",
      CHAIN_POWER_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.printf("[ChainOSCnano][BOOT] chain_uart=RX%u/TX%u enabled=%s\n",
                CHAIN_RX_PIN, CHAIN_TX_PIN,
                CHAIN_UART_ENABLED ? "true" : "false");
  Serial.println("[ChainOSCnano][BOOT] READY");
}

void printHeartbeat() {
  Serial.printf("[ChainOSCnano][RUN] uptime=%lu ms free_heap=%lu bytes\n",
                static_cast<unsigned long>(millis()),
                static_cast<unsigned long>(ESP.getFreeHeap()));
}

