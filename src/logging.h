#pragma once

#include "config.h"

// Detailed per-device diagnostics are useful during bring-up but consume
// several kilobytes of the ESP32-C6 application partition. Keep production
// builds compact while preserving the call sites for future diagnostics.
#define NANO_VERBOSE_LOGF(...) do { } while (0)
#define NANO_VERBOSE_PRINT(...) do { } while (0)
#define NANO_VERBOSE_PRINTLN(...) do { } while (0)

#if CHAINOSCNANO_STORAGE_DEBUG
#define NANO_STORAGE_LOGF(...) Serial.printf(__VA_ARGS__)
#else
#define NANO_STORAGE_LOGF(...) do { } while (0)
#endif
