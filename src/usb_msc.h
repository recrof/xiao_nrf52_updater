#pragma once

namespace usb_msc {

void begin();

// Has the host ever mounted the drive this session?
bool was_ever_mounted();

// Is the host currently mounted?
bool is_mounted();

// True after the host has issued a SCSI Start-Stop-Unit with load_eject=1.
// This is what macOS / Finder send on Eject (the USB device stays plugged
// in, so tud_umount_cb does NOT fire). Once latched, stays true.
bool was_ejected();

// Disable / re-enable the MSC LUN. We disable while running DFU so the host
// can't write to the flash mid-operation.
void set_ready(bool ready);

}  // namespace usb_msc
