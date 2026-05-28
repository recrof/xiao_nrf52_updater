#pragma once

#include <Arduino.h>
#include <bluefruit.h>

namespace ble_scanner {

struct Target {
  ble_gap_addr_t addr;        // 48-bit MAC + type, ready to pass to Central.connect()
  int8_t         rssi;
  char           name[24];    // empty string if device didn't advertise a name
};

// Initialize the BLE central stack. Must be called once before scan() and the
// later DFU client work. Safe to call after USB is up.
void begin();

// Scan for any device advertising the Nordic Legacy DFU service UUID
// (00001530-1212-EFDE-1523-785FEABCD123). Blocks until either a match is
// found or `timeout_ms` elapses. Returns true on match, false on timeout.
//
// `name_filter`, when non-empty, is matched as a substring against the
// advertised Complete / Short Local Name; non-matching ads are ignored.
// `min_rssi` rejects ads weaker than the threshold (in dBm, negative). Pass
// -127 for no signal-strength filter.
bool find_first(Target* out, uint32_t timeout_ms,
                const char* name_filter = nullptr,
                int8_t min_rssi = -127);

}  // namespace ble_scanner
