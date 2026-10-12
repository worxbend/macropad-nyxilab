#include "usb_hid.h"

#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

#include "config.h"

namespace hw {
namespace {

enum : uint8_t { RID_KEYBOARD = 1, RID_MOUSE, RID_CONSUMER };

uint8_t const kReportDesc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(RID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(RID_MOUSE)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(RID_CONSUMER)),
};

Adafruit_USBD_HID g_hid;

mp::HidState g_want{}, g_sent{};
uint8_t g_extra_buttons = 0, g_sent_buttons = 0;
// Preserve press/release order while the shared endpoint is busy. Generated taps
// and macros also wait for delivery, so they cannot run ahead of this queue.
constexpr uint8_t kQueueSize = 64;
mp::HidState g_queue[kQueueSize];
uint8_t g_head = 0, g_count = 0;
uint8_t g_resync = 0;  // keyboard / consumer / mouse must be sent after enumeration
bool g_mounted = false, g_ever_mounted = false;
bool g_state_inflight = false;  // motion-only reports must not pause macros/tap timers
int16_t g_dx = 0, g_dy = 0, g_wheel = 0, g_pan = 0;
volatile uint8_t g_leds = 0;

void on_set_report(uint8_t report_id, hid_report_type_t type, uint8_t const* buf, uint16_t len) {
  if (type != HID_REPORT_TYPE_OUTPUT || len == 0) return;
  // Some hosts prefix the report id when it arrives on the OUT endpoint.
  if (report_id == 0 && len >= 2 && buf[0] == RID_KEYBOARD) {
    g_leds = buf[1];
  } else if (report_id == RID_KEYBOARD || report_id == 0) {
    g_leds = buf[0];
  }
}

int8_t clamp8(int16_t v) { return int8_t(v > 127 ? 127 : (v < -127 ? -127 : v)); }

void enqueue_current() {
  mp::HidState s = g_want;
  s.mouse_buttons |= g_extra_buttons;
  mp::HidState last = g_sent;
  last.mouse_buttons = g_sent_buttons;
  if (g_count) last = g_queue[(g_head + g_count - 1) % kQueueSize];
  if (s == last || g_count == kQueueSize) return;
  g_queue[(g_head + g_count) % kQueueSize] = s;
  g_count++;
}

}  // namespace

void usb_begin() {
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  TinyUSBDevice.setManufacturerDescriptor(cfg::USB_VENDOR_NAME);
  TinyUSBDevice.setProductDescriptor(cfg::USB_PRODUCT_NAME);
  g_hid.setPollInterval(1);
  g_hid.setReportDescriptor(kReportDesc, sizeof(kReportDesc));
  g_hid.setStringDescriptor(cfg::USB_PRODUCT_NAME);
  g_hid.setReportCallback(nullptr, on_set_report);
  g_hid.begin();
  // the core enumerated CDC before setup(); re-enumerate so the host sees the HID interface
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

void usb_set_keyboard(const mp::HidState& s) {
  g_want = s;
  enqueue_current();
}

void usb_set_extra_buttons(uint8_t buttons) {
  g_extra_buttons = buttons;
  enqueue_current();
}

void usb_add_mouse(int8_t dx, int8_t dy, int8_t wheel, int8_t pan) {
  g_dx += dx;
  g_dy += dy;
  g_wheel += wheel;
  g_pan += pan;
}

void usb_task() {
  const bool mounted = TinyUSBDevice.mounted();
  if (mounted && !g_mounted) {
    // A new host session has no knowledge of the previous reports. On remount,
    // discard old-session transitions and publish the currently held inputs.
    if (g_ever_mounted) g_head = g_count = 0;
    g_ever_mounted = true;
    g_resync = 7;
    if (!g_count) {
      g_queue[g_head] = g_want;
      g_queue[g_head].mouse_buttons |= g_extra_buttons;
      g_count = 1;
    }
  }
  g_mounted = mounted;
  if (!mounted || TinyUSBDevice.suspended()) {
    // don't let stick motion pile up while nobody is listening (no jump on wake-up)
    g_dx = g_dy = g_wheel = g_pan = 0;
    if (mounted && g_count) TinyUSBDevice.remoteWakeup();
    return;
  }
  enqueue_current();  // after overflow, eventually send the latest state (including releases)
  if (!g_count && !g_dx && !g_dy && !g_wheel && !g_pan) return;
  if (!g_hid.ready()) return;
  g_state_inflight = false;
  const mp::HidState& target = g_count ? g_queue[g_head] : g_sent;
  // Finish this snapshot before advancing to the next press or release.
  if ((g_resync & 1) || target.mods != g_sent.mods || memcmp(target.keys, g_sent.keys, 6) != 0) {
    uint8_t keys[6];
    memcpy(keys, target.keys, 6);
    if (g_hid.keyboardReport(RID_KEYBOARD, target.mods, keys)) {
      g_sent.mods = target.mods;
      memcpy(g_sent.keys, target.keys, 6);
      g_resync &= ~1u;
      g_state_inflight = true;
    }
    return;
  }
  if ((g_resync & 2) || target.consumer != g_sent.consumer) {
    if (g_hid.sendReport16(RID_CONSUMER, target.consumer)) {
      g_sent.consumer = target.consumer;
      g_resync &= ~2u;
      g_state_inflight = true;
    }
    return;
  }
  const uint8_t buttons = g_count ? target.mouse_buttons : g_sent_buttons;
  const int8_t dx = clamp8(g_dx), dy = clamp8(g_dy), w = clamp8(g_wheel), p = clamp8(g_pan);
  const bool buttons_pending = (g_resync & 4) || buttons != g_sent_buttons;
  const bool mouse_pending = buttons_pending || dx || dy || w || p;
  if (mouse_pending && !g_hid.mouseReport(RID_MOUSE, buttons, dx, dy, w, p)) return;
  if (mouse_pending) {
    g_dx -= dx;
    g_dy -= dy;
    g_wheel -= w;
    g_pan -= p;
    g_sent_buttons = buttons;
    g_resync &= ~4u;
    g_state_inflight = buttons_pending;
  }
  if (g_count) {
    g_head = (g_head + 1) % kQueueSize;
    g_count--;
    enqueue_current();
  }
}

bool usb_keyboard_delivered() {
  return TinyUSBDevice.mounted() && !TinyUSBDevice.suspended() && !g_count && !g_resync &&
         (!g_state_inflight || g_hid.ready());
}

bool usb_mounted() { return TinyUSBDevice.mounted(); }
bool usb_suspended() { return TinyUSBDevice.suspended(); }
uint8_t usb_leds() { return g_leds; }

}  // namespace hw
