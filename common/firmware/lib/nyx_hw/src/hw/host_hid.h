// The host bridge's USB channel: a second, vendor-defined HID interface
// (usage page 0xFF4E) with 64-byte reports in both directions.  Linux exposes it
// as /dev/hidrawN without any driver; the daemon in host/ talks to it.  The
// packets themselves are handled by core/host_proto.
#pragma once
#include <stdint.h>

#include "core/host_proto.h"

namespace hw {

// Adds the interface; call before usb_begin(), which re-enumerates with it.
void host_hid_begin();
bool host_hid_read(uint8_t (&pkt)[mp::HOST_PACKET], uint16_t& len);  // next packet from the host
bool host_hid_write(const uint8_t (&pkt)[mp::HOST_PACKET]);          // false = busy / not connected

}  // namespace hw
