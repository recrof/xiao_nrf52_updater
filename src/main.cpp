#include <Arduino.h>
// IMPORTANT: must be in the same TU as setup()/Serial so the BSP swaps Serial
// to TinyUSB CDC. Without this, USB-MSC and CDC silently fail to enumerate.
#include <Adafruit_TinyUSB.h>
#include <nrf_power.h>

#include "ble_scanner.h"
#include "config.h"
#include "dfu_legacy.h"
#include "firmware_zip.h"
#include "logger.h"
#include "storage.h"
#include "usb_msc.h"
#include "zip_reader.h"

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------
// Indicator scheme:
//   BLUE  slow blink     = idle, waiting for the host
//   BLUE  solid          = host has the drive mounted
//   GREEN fast blink     = DFU running
//   GREEN solid          = DFU succeeded
//   RED   solid          = DFU failed (after 3 retries) / boot error

static void led_set(uint8_t pin, bool on) { digitalWrite(pin, on ? LED_STATE_ON : !LED_STATE_ON); }

static void leds_off() {
  pinMode(LED_RED,   OUTPUT); led_set(LED_RED,   false);
  pinMode(LED_GREEN, OUTPUT); led_set(LED_GREEN, false);
  pinMode(LED_BLUE,  OUTPUT); led_set(LED_BLUE,  false);
}

// ---------------------------------------------------------------------------
// Boot conditions
// ---------------------------------------------------------------------------
// VBUS detect — bit set when USB is supplying power. If absent, the board is
// running off the BAT+/BAT- pads and we should treat any zip on the drive as
// "ready to flash immediately", matching the requirements' physical-unplug
// trigger.
static bool vbus_present() {
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum class State : uint8_t {
  kIdle,          // waiting for trigger
  kRunning,       // DFU sequence in progress (we block in here)
  kDoneOk,        // success — green solid
  kDoneFail,      // failed after retries — red solid
};

static State                  s_state     = State::kIdle;
static bool                   s_storage_ok = false;
static bool                   s_armed_boot = false;  // VBUS-absent trigger fired
static firmware_zip::Parsed   s_bundle;
static volatile uint8_t       s_progress_pct = 0;     // 0..100, updated from dfu_legacy

static void leds_tick(uint32_t now);  // forward decl

static void on_dfu_progress(uint8_t pct) {
  s_progress_pct = pct;
  leds_tick(millis());
}

// Try one full DFU attempt against the *current* contents of the drive.
// Returns true on success. Handles buttonless trigger internally by
// rescanning after the peer reboots.
static bool run_dfu_attempt(const char* zip_name) {
  char err[80];
  if (!firmware_zip::parse(zip_name, &s_bundle, err, sizeof(err))) {
    logger::log("dfu: zip parse failed: %s", err);
    return false;
  }
  logger::log("dfu: bundle type=0x%02x bin=%lu B dat=%lu B",
              s_bundle.type,
              (unsigned long)s_bundle.bin.size,
              (unsigned long)s_bundle.dat.size);

  const config::Config& cfg = config::current();

  // Scan + connect cycle. Loops once for the buttonless transition.
  for (int round = 0; round < 2; round++) {
    ble_scanner::Target t;
    if (!ble_scanner::find_first(&t, 10000, cfg.ble_name, cfg.min_rssi)) {
      logger::log("dfu: no DFU advertisers seen in 10 s");
      zip_reader::close();
      return false;
    }
    logger::log("dfu: target %02X:%02X:%02X:%02X:%02X:%02X  rssi=%d  '%s'",
                t.addr.addr[5], t.addr.addr[4], t.addr.addr[3],
                t.addr.addr[2], t.addr.addr[1], t.addr.addr[0],
                t.rssi, t.name);

    dfu_legacy::Result r = dfu_legacy::run(t, s_bundle, cfg);
    if (r == dfu_legacy::Result::kButtonlessTriggered) {
      logger::log("dfu: buttonless triggered, waiting for peer to reboot...");
      delay(2000);
      continue;
    }

    zip_reader::close();
    if (r == dfu_legacy::Result::kOk) return true;
    logger::log("dfu: attempt failed with result=%d", (int)r);
    return false;
  }
  zip_reader::close();
  return false;
}

// Run up to 3 attempts. Deletes the zip on success.
static void run_dfu_sequence() {
  char zip_name[64];
  int n = storage::find_single_zip(zip_name, sizeof(zip_name));
  if (n <= 0) {
    logger::log("dfu: trigger but no zip on drive (count=%d)", n);
    s_state = State::kDoneFail;
    return;
  }
  if (n > 1) {
    logger::log("dfu: %d zips on drive, expected exactly 1", n);
    s_state = State::kDoneFail;
    return;
  }

  const config::Config& cfg = config::current();
  const uint8_t  retries  = cfg.retries;
  const uint16_t cooldown = cfg.retry_cooldown;
  logger::log("dfu: starting sequence with %s (max %u attempts)", zip_name, retries);
  for (uint8_t attempt = 1; attempt <= retries; attempt++) {
    logger::log("dfu: --- attempt %u/%u ---", attempt, retries);
    if (run_dfu_attempt(zip_name)) {
      logger::log("dfu: SUCCESS, deleting %s", zip_name);
      if (!storage::delete_file(zip_name)) {
        logger::log("dfu: WARNING: failed to delete %s", zip_name);
      }
      s_state = State::kDoneOk;
      return;
    }
    if (attempt < retries && cooldown > 0) {
      logger::log("dfu: cooldown %u s before next attempt", cooldown);
      delay((uint32_t)cooldown * 1000);
    }
  }
  logger::log("dfu: FAILED after %u attempts", retries);
  s_state = State::kDoneFail;
}

// ---------------------------------------------------------------------------
// LED tick
// ---------------------------------------------------------------------------
static void leds_tick(uint32_t now) {
  static uint32_t last  = 0;
  static bool     phase = false;

  switch (s_state) {
    case State::kIdle:
      if (usb_msc::is_mounted()) {
        led_set(LED_RED,   false);
        led_set(LED_GREEN, false);
        led_set(LED_BLUE,  true);
      } else {
        if (now - last >= 500) { last = now; phase = !phase; }
        led_set(LED_RED,   false);
        led_set(LED_GREEN, false);
        led_set(LED_BLUE,  phase);
      }
      break;
    case State::kRunning: {
      // Green blink whose period shortens as progress climbs. At 0% the
      // half-period is 600 ms (slow flash); by ~95% it's near 30 ms so the
      // LED looks solid to the eye. 100% pins it on.
      uint8_t  pct       = s_progress_pct;
      uint32_t half_ms;
      if (pct >= 100) {
        led_set(LED_RED, false);
        led_set(LED_GREEN, true);
        led_set(LED_BLUE, false);
        break;
      }
      half_ms = 600 - ((uint32_t)pct * (600 - 30)) / 100;  // 600..30 ms
      if (now - last >= half_ms) { last = now; phase = !phase; }
      led_set(LED_RED,   false);
      led_set(LED_GREEN, phase);
      led_set(LED_BLUE,  false);
      break;
    }
    case State::kDoneOk:
      led_set(LED_RED,   false);
      led_set(LED_GREEN, true);
      led_set(LED_BLUE,  false);
      break;
    case State::kDoneFail:
      led_set(LED_RED,   true);
      led_set(LED_GREEN, false);
      led_set(LED_BLUE,  false);
      break;
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
  leds_off();
  Serial.begin(115200);

  bool vbus = vbus_present();

  s_storage_ok = storage::begin();
  // MSC is only useful while USB is connected; expose it unconditionally so
  // the rare "plugged in after boot" case still works.
  usb_msc::begin();
  ble_scanner::begin();
  dfu_legacy::set_progress_callback(on_dfu_progress);

  bool cfg_loaded = false;
  if (s_storage_ok) cfg_loaded = config::load();
  const config::Config& cfg = config::current();

  logger::log("boot: storage=%s vbus=%d cfg=%s",
              s_storage_ok ? "ok" : "fail", (int)vbus,
              cfg_loaded ? "CONFIG.TXT" : "defaults");
  logger::log("cfg:  ble_name='%s' prn=%u high_mtu=%d retries=%u min_rssi=%d retry_cooldown=%u",
              cfg.ble_name, cfg.prn, (int)cfg.high_mtu, cfg.retries, (int)cfg.min_rssi, cfg.retry_cooldown);

  // Boot-without-USB-power trigger: if we came up on battery and there's
  // already a zip on the drive, jump straight into DFU. This matches the
  // requirements' "physical unplug → board flashes target" workflow when
  // the XIAO has a battery wired to BAT+/BAT-.
  if (!vbus && s_storage_ok) {
    char zip_name[64];
    if (storage::find_single_zip(zip_name, sizeof(zip_name)) == 1) {
      logger::log("boot: no VBUS + zip present, arming DFU");
      s_armed_boot = true;
    }
  }
}

void loop() {
  uint32_t now = millis();
  leds_tick(now);

  if (s_state == State::kIdle) {
    bool eject_trig = s_storage_ok && usb_msc::was_ejected();
    if (eject_trig || s_armed_boot) {
      logger::log("dfu: trigger %s", eject_trig ? "eject" : "boot-no-vbus");
      s_armed_boot = false;
      s_state      = State::kRunning;
      run_dfu_sequence();   // blocking, sets s_state to kDoneOk / kDoneFail
    }
  }
}
