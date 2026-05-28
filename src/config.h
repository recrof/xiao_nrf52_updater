#pragma once

#include <Arduino.h>

namespace config {

struct Config {
  // Substring filter for advertised BLE name. Empty = accept any peer that
  // exposes the Legacy DFU service.
  char     ble_name[24];

  // Packet Receipt Notification cadence (writes between PRN callbacks).
  // 10 is safe for SDK 6.0 bootloaders; modern ones tolerate up to ~32.
  // 0 disables PRNs entirely (faster but no flow control — risky).
  uint16_t prn;

  // Negotiate ATT MTU up to 247 B after connect. When false, falls back to
  // the default MTU of 23 (20 B payload per write). Older bootloaders may
  // not honour MTU exchange — set to false if a target stalls.
  bool     high_mtu;

  // Number of DFU attempts before giving up.
  uint8_t  retries;

  // Minimum RSSI (dBm, negative) we'll accept from a target. Ads weaker than
  // this are ignored during the scan phase. Default -127 = no minimum.
  // Useful when this rig is mounted on a drone and we want to refuse flashing
  // a peer that's too far / signal too weak to reliably stream to.
  int8_t   min_rssi;

  // Cooldown between failed attempts, in seconds. The DFU bootloader needs
  // a moment to settle after a reset before it will accept another START_DFU,
  // and slamming retries returns INVALID_STATE.
  uint16_t retry_cooldown;
};

// Set the working config from defaults, then overlay anything found in
// /CONFIG.TXT on the drive. Always succeeds; missing file = defaults only.
// Returns true if CONFIG.TXT was loaded, false if defaults were used.
bool load();

const Config& current();

}  // namespace config
