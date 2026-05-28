#include "ble_scanner.h"

#include "logger.h"

namespace ble_scanner {

// Nordic Legacy DFU service UUID (LSB first because BLEUuid takes a 16-byte
// LE array): 00001530-1212-EFDE-1523-785FEABCD123
static const uint8_t kLegacyDfuUuid128[16] = {
  0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15,
  0xDE, 0xEF, 0x12, 0x12, 0x30, 0x15, 0x00, 0x00,
};

static volatile bool s_found = false;
static Target        s_match;
static const char*   s_name_filter = nullptr;   // nullptr or "" = accept any
static int8_t        s_min_rssi    = -127;      // -127 = no minimum

static void scan_cb(ble_gap_evt_adv_report_t* report) {
  if (s_found) {
    Bluefruit.Scanner.resume();
    return;
  }

  // Quick check: is the Legacy DFU service UUID present in the advert?
  // Bluefruit's helper handles both "complete" and "incomplete" UUID lists
  // and either 16-bit or 128-bit forms.
  BLEUuid uuid(kLegacyDfuUuid128);
  if (!Bluefruit.Scanner.checkReportForUuid(report, uuid)) {
    Bluefruit.Scanner.resume();
    return;
  }

  // RSSI threshold rejects weak ads early, before we even parse the name.
  if (report->rssi < s_min_rssi) {
    Bluefruit.Scanner.resume();
    return;
  }

  Target candidate{};
  candidate.addr = report->peer_addr;
  candidate.rssi = report->rssi;
  Bluefruit.Scanner.parseReportByType(
      report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
      (uint8_t*)candidate.name, sizeof(candidate.name) - 1);
  if (candidate.name[0] == '\0') {
    Bluefruit.Scanner.parseReportByType(
        report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME,
        (uint8_t*)candidate.name, sizeof(candidate.name) - 1);
  }
  candidate.name[sizeof(candidate.name) - 1] = '\0';

  // Optional substring name filter from config.
  if (s_name_filter && s_name_filter[0] != '\0') {
    if (strstr(candidate.name, s_name_filter) == nullptr) {
      Bluefruit.Scanner.resume();
      return;
    }
  }

  s_match  = candidate;
  s_found  = true;
  Bluefruit.Scanner.stop();
}

void begin() {
  // The SoftDevice's ATT buffer cap is fixed at Bluefruit.begin() time.
  // Default is 23 (MTU=23). To make later MTU-exchange requests effective,
  // we have to pre-allocate buffer space for the maximum we ever want:
  //   mtu_max=247        : allow up to 247 B per ATT packet
  //   event_len=6        : 6 LL units per connection event (more bandwidth)
  //   hvn_qsize=2        : modest notification queue
  //   wrcmd_qsize=4      : 4 outstanding WRITE_CMD packets (streaming throughput)
  // These are applied to the central role; we don't run as peripheral.
  Bluefruit.configCentralConn(247, 6, 2, 4);

  // 0 peripheral, 1 central — we're a pure DFU client.
  Bluefruit.begin(0, 1);
  Bluefruit.setName("XIAO DFU updater");
  Bluefruit.Scanner.setRxCallback(scan_cb);
  Bluefruit.Scanner.restartOnDisconnect(false);
  Bluefruit.Scanner.useActiveScan(true);
  Bluefruit.Scanner.setInterval(160, 80);  // 100 ms window / 200 ms interval
}

bool find_first(Target* out, uint32_t timeout_ms, const char* name_filter, int8_t min_rssi) {
  s_found       = false;
  s_name_filter = name_filter;
  s_min_rssi    = min_rssi;
  memset(&s_match, 0, sizeof(s_match));

  Bluefruit.Scanner.start(0);  // 0 = scan until told to stop

  uint32_t deadline = millis() + timeout_ms;
  while (!s_found && (int32_t)(deadline - millis()) > 0) {
    delay(50);
  }

  Bluefruit.Scanner.stop();

  if (!s_found) return false;
  *out = s_match;
  return true;
}

}  // namespace ble_scanner
