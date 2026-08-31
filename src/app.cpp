#include "app.h"

#include <Arduino.h>

#include "chain_probe.h"
#include "config.h"
#include "diagnostics.h"
#include "nano_hardware.h"
#include "network_manager.h"
#include "osc_manager.h"
#include "system_settings.h"

namespace {

bool bootDiagnosticsPrinted = false;
unsigned long lastHeartbeatMs = 0;

}  // namespace

void appSetup() {
  Serial.begin(SERIAL_BAUD);
  delay(250);

  nanoHardwareSetup();
  systemSettingsSetup();
  oscSetup();
  networkSetup();
  chainProbeSetup();
}

void appLoop() {
  const unsigned long now = millis();
  networkUpdate();
  nanoHardwareUpdate();
  chainProbeUpdate();

  if (!bootDiagnosticsPrinted && now >= BOOT_DIAGNOSTICS_DELAY_MS) {
    bootDiagnosticsPrinted = true;
    printBootDiagnostics();
    printHeartbeat();
    lastHeartbeatMs = now;
  } else if (bootDiagnosticsPrinted &&
             now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatMs = now;
    printHeartbeat();
  }
  delay(1);
}
