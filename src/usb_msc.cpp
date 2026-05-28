#include "usb_msc.h"

#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

#include "storage.h"

// File-static state. Accessed both from the namespace API and from the C-linkage
// TinyUSB callbacks below, so it lives at file scope rather than inside the namespace.
static Adafruit_USBD_MSC s_msc;
static volatile bool     s_mounted      = false;
static volatile bool     s_ever_mounted = false;
static volatile bool     s_ready        = true;
static volatile bool     s_ejected      = false;

static int32_t on_read(uint32_t lba, void* buffer, uint32_t bufsize) {
  return storage::flash().readBlocks(lba, (uint8_t*)buffer, bufsize / 512) ? (int32_t)bufsize : -1;
}

static int32_t on_write(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
  return storage::flash().writeBlocks(lba, buffer, bufsize / 512) ? (int32_t)bufsize : -1;
}

static void on_flush() {
  storage::flash().syncBlocks();
  // Drop any in-RAM FAT cache so a subsequent firmware-side read sees the host's writes.
  storage::fs().cacheClear();
}

static bool on_start_stop(uint8_t power_condition, bool start, bool load_eject) {
  (void)power_condition;
  if (load_eject && !start) {
    s_ejected = true;
  }
  return true;
}

namespace usb_msc {

void begin() {
  s_msc.setID("XIAO", "DFU Updater", "1.0");
  s_msc.setReadWriteCallback(on_read, on_write, on_flush);
  // macOS / Linux / Windows all send SCSI START_STOP_UNIT(load_eject=1, start=0)
  // when the user ejects. The USB cable stays connected, so we have to hook
  // this rather than tud_umount_cb.
  s_msc.setStartStopCallback(on_start_stop);

  uint32_t blocks = storage::flash().size() / 512;
  Serial.print("usb_msc: capacity = ");
  Serial.print(blocks);
  Serial.println(" * 512 B");

  s_msc.setCapacity(blocks, 512);
  s_msc.setUnitReady(s_ready);

  bool ok = s_msc.begin();
  Serial.print("usb_msc: s_msc.begin() -> ");
  Serial.println(ok ? "ok" : "FAIL");
}

bool is_mounted()       { return s_mounted; }
bool was_ever_mounted() { return s_ever_mounted; }
bool was_ejected()      { return s_ejected; }

void set_ready(bool ready) {
  s_ready = ready;
  s_msc.setUnitReady(ready);
}

}  // namespace usb_msc

// TinyUSB device-state callbacks (C linkage, weak in the SDK).
extern "C" {

void tud_mount_cb(void) {
  s_mounted      = true;
  s_ever_mounted = true;
}

void tud_umount_cb(void) {
  s_mounted = false;
}

}  // extern "C"
