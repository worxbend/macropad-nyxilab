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
bool g_kb_dirty = false, g_cc_dirty = false, g_btn_dirty = false;
uint8_t g_extra_buttons = 0, g_sent_buttons = 0;
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
  if (s.mods != g_sent.mods || memcmp(s.keys, g_sent.keys, 6) != 0) g_kb_dirty = true;
  if (s.consumer != g_sent.consumer) g_cc_dirty = true;
  if (uint8_t(s.mouse_buttons | g_extra_buttons) != g_sent_buttons) g_btn_dirty = true;
  g_want = s;
}

void usb_set_extra_buttons(uint8_t buttons) {
  g_extra_buttons = buttons;
  if (uint8_t(g_want.mouse_buttons | g_extra_buttons) != g_sent_buttons) g_btn_dirty = true;
}

void usb_add_mouse(int8_t dx, int8_t dy, int8_t wheel, int8_t pan) {
  g_dx += dx;
  g_dy += dy;
  g_wheel += wheel;
  g_pan += pan;
}

void usb_task() {
  const bool pending = g_kb_dirty || g_cc_dirty || g_btn_dirty || g_dx || g_dy || g_wheel || g_pan;
  if (!pending) return;
  if (!TinyUSBDevice.mounted() || TinyUSBDevice.suspended()) {
    // don't let stick motion pile up while nobody is listening (no jump on wake-up)
    g_dx = g_dy = g_wheel = g_pan = 0;
    if (TinyUSBDevice.mounted() && (g_kb_dirty || g_cc_dirty || g_btn_dirty)) TinyUSBDevice.remoteWakeup();
    return;
  }
  if (!g_hid.ready()) return;
  // one report per call, keyboard first
  if (g_kb_dirty) {
    uint8_t keys[6];
    memcpy(keys, g_want.keys, 6);
    if (g_hid.keyboardReport(RID_KEYBOARD, g_want.mods, keys)) {
      g_sent.mods = g_want.mods;
      memcpy(g_sent.keys, g_want.keys, 6);
      g_kb_dirty = false;
    }
    return;
  }
  if (g_cc_dirty) {
    if (g_hid.sendReport16(RID_CONSUMER, g_want.consumer)) {
      g_sent.consumer = g_want.consumer;
      g_cc_dirty = false;
    }
    return;
  }
  const uint8_t buttons = g_want.mouse_buttons | g_extra_buttons;
  const int8_t dx = clamp8(g_dx), dy = clamp8(g_dy), w = clamp8(g_wheel), p = clamp8(g_pan);
  if (g_hid.mouseReport(RID_MOUSE, buttons, dx, dy, w, p)) {
    g_dx -= dx;
    g_dy -= dy;
    g_wheel -= w;
    g_pan -= p;
    g_sent_buttons = buttons;
    g_btn_dirty = false;
  }
}

bool usb_mounted() { return TinyUSBDevice.mounted(); }
bool usb_suspended() { return TinyUSBDevice.suspended(); }
uint8_t usb_leds() { return g_leds; }

}  // namespace hw
