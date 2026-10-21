#include "host_hid.h"

#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <atomic>

namespace hw {
namespace {

// Vendor collection: 64 bytes in, 64 bytes out, no report id.  The interface
// string lets udev rules and the daemon find this interface (and not the keyboard).
uint8_t const kReportDesc[] = {
    0x06, uint8_t(mp::HOST_USAGE_PAGE & 0xFF), uint8_t(mp::HOST_USAGE_PAGE >> 8),  // Usage Page (vendor)
    0x09, uint8_t(mp::HOST_USAGE),                                                 // Usage
    0xA1, 0x01,                                                                    // Collection (Application)
    0x09, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, mp::HOST_PACKET,   //   64 x 8 bit
    0x81, 0x02,                                                                    //   Input (Data, Var, Abs)
    0x09, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, mp::HOST_PACKET,   //   64 x 8 bit
    0x91, 0x02,                                                                    //   Output (Data, Var, Abs)
    0xC0,                                                                          // End Collection
};

Adafruit_USBD_HID g_vendor;

// Packets arrive in TinyUSB's task, a software interrupt on core 0: a
// single-producer / single-consumer ring between it and the main loop.
constexpr uint8_t kSlots = 4;
uint8_t g_rx[kSlots][mp::HOST_PACKET];
uint16_t g_rx_len[kSlots];
std::atomic<uint8_t> g_rx_head{0}, g_rx_tail{0};

void on_set_report(uint8_t report_id, hid_report_type_t type, uint8_t const* buf, uint16_t len) {
  (void)report_id;
  (void)type;  // OUT endpoint data and SET_REPORT both carry a packet
  const uint8_t head = g_rx_head.load(std::memory_order_relaxed);
  const uint8_t next = uint8_t((head + 1) % kSlots);
  if (next == g_rx_tail.load(std::memory_order_acquire) || !len) return;  // full: the daemon resends every second
  const uint16_t n = len < mp::HOST_PACKET ? len : mp::HOST_PACKET;
  memcpy(g_rx[head], buf, n);
  g_rx_len[head] = n;
  g_rx_head.store(next, std::memory_order_release);
}

}  // namespace

void host_hid_begin() {
  g_vendor.setPollInterval(4);
  g_vendor.setReportDescriptor(kReportDesc, sizeof(kReportDesc));
  g_vendor.setStringDescriptor("Nyxilab Host Link");
  g_vendor.enableOutEndpoint(true);
  g_vendor.setReportCallback(nullptr, on_set_report);
  g_vendor.begin();
}

bool host_hid_read(uint8_t (&pkt)[mp::HOST_PACKET], uint16_t& len) {
  const uint8_t tail = g_rx_tail.load(std::memory_order_relaxed);
  if (tail == g_rx_head.load(std::memory_order_acquire)) return false;
  len = g_rx_len[tail];
  memcpy(pkt, g_rx[tail], len);
  g_rx_tail.store(uint8_t((tail + 1) % kSlots), std::memory_order_release);
  return true;
}

bool host_hid_write(const uint8_t (&pkt)[mp::HOST_PACKET]) {
  if (!TinyUSBDevice.mounted() || TinyUSBDevice.suspended() || !g_vendor.ready()) return false;
  return g_vendor.sendReport(0, pkt, mp::HOST_PACKET);
}

}  // namespace hw
