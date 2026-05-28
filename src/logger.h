#pragma once

#include <Arduino.h>

namespace logger {

// Append a line to LOG.TXT on the QSPI drive and mirror it to Serial.
// Timestamp is prepended in `[ms]` form (boot-relative, no RTC).
// Safe to call before / after the host has the drive mounted; we never hold
// the file open across calls, so the host's view stays consistent.
void log(const char* fmt, ...);

}  // namespace logger
