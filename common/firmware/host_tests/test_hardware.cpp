// Compile production hardware/control code against narrow Arduino/USB/EEPROM mocks.
#include <cassert>
#include <cstdio>
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <EEPROM.h>
#include "config.h"
#include "core/host_proto.h"
#include "hw/host_hid.h"
#include "hw/settings.h"
#include "hw/usb_hid.h"
#ifdef TEST_JOYSTICK
#include "stick.h"
using Centre = JoystickCentre;
#else
#include "knob.h"
using Centre = KnobCentre;
#endif

namespace app {
void activity(uint32_t) {}
void hit(uint32_t) {}
void settings_changed(uint32_t now) { hw::settings_touch(now); }
}
struct Host : mp::EngineHost {
  void on_sys(mp::SysCmd) override {}
  void on_centre(uint8_t) override {}
};

static void drain() {
  for (int i = 0; i < 1000; ++i) {
    mock_ready = true;  // previous transfer completed; one new report per USB frame
    hw::usb_task();
    if (hw::usb_keyboard_delivered()) return;
  }
  assert(false && "HID queue failed to drain");
}

static void test_usb_preserves_press_and_release() {
  drain();
  mock_reports.clear();
  mock_ready = false;
  mp::HidState state;
  state.keys[0] = mp::hid::A;
  state.consumer = mp::cc::VOL_UP;
  state.mouse_buttons = mp::MB_LEFT;
  hw::usb_set_keyboard(state);
  hw::usb_task();
  hw::usb_set_keyboard({});
  assert(mock_reports.empty());
  drain();
  assert(mock_reports.size() == 6);
  assert(mock_reports[0].id == 1 && mock_reports[0].keys[0] == mp::hid::A);
  assert(mock_reports[1].id == 3 && mock_reports[1].value == mp::cc::VOL_UP);
  assert(mock_reports[2].id == 2 && mock_reports[2].value == mp::MB_LEFT);
  assert(mock_reports[3].id == 1 && mock_reports[3].keys[0] == 0);
  assert(mock_reports[4].id == 3 && mock_reports[4].value == 0);
  assert(mock_reports[5].id == 2 && mock_reports[5].value == 0);
}

static void test_usb_retries_failed_reports() {
  mock_reports.clear();
  mp::HidState state;
  state.consumer = mp::cc::MUTE;
  hw::usb_set_keyboard(state);
  mock_accept = false;
  for (int i = 0; i < 20; ++i) hw::usb_task();
  assert(mock_reports.empty());
  assert(!hw::usb_keyboard_delivered());
  hw::usb_set_keyboard({});
  mock_accept = true;
  drain();
  assert(mock_reports.size() == 2);
  assert(mock_reports[0].value == mp::cc::MUTE && mock_reports[1].value == 0);
}

static void test_usb_resends_after_remount() {
  mp::HidState state;
  state.keys[0] = mp::hid::B;
  state.consumer = mp::cc::MUTE;
  state.mouse_buttons = mp::MB_RIGHT;
  hw::usb_set_keyboard(state);
  drain();
  mock_reports.clear();
  TinyUSBDevice.connected = false;
  hw::usb_task();
  assert(!hw::usb_keyboard_delivered());
  TinyUSBDevice.connected = true;
  drain();
  assert(mock_reports.size() == 3);
  assert(mock_reports[0].keys[0] == mp::hid::B);
  assert(mock_reports[1].value == mp::cc::MUTE);
  assert(mock_reports[2].value == mp::MB_RIGHT);
  hw::usb_set_keyboard({});
  drain();
}

static void test_usb_overflow_eventually_releases() {
  mock_ready = false;
  for (int i = 0; i < 100; ++i) {
    mp::HidState state;
    state.consumer = mp::cc::VOL_UP;
    hw::usb_set_keyboard(state);
    hw::usb_set_keyboard({});
  }
  drain();
  assert(mock_reports.back().value == 0);
}

static void test_motion_does_not_pause_generated_keys() {
  drain();
  hw::usb_add_mouse(1, 0, 0, 0);
  hw::usb_task();
  assert(!mock_ready);  // motion transfer is still in flight
  assert(hw::usb_keyboard_delivered());
  drain();
}

static void test_suspended_click_requests_wake() {
  drain();
  TinyUSBDevice.asleep = true;
  const auto wakes = TinyUSBDevice.wakes;
  hw::usb_set_extra_buttons(mp::MB_LEFT);
  hw::usb_task();
  assert(TinyUSBDevice.wakes == wakes + 1);
  assert(!hw::usb_keyboard_delivered());
  hw::usb_set_extra_buttons(0);
  TinyUSBDevice.asleep = false;
  mock_reports.clear();
  drain();
  assert(mock_reports.size() == 2);
  assert(mock_reports[0].value == mp::MB_LEFT && mock_reports[1].value == 0);
}

static void test_settings_isolate_editions() {
  hw::settings_load();
  auto saved = hw::settings();
  saved.brightness = 77;
  saved.edition = cfg::SETTINGS_EDITION == 1 ? 2 : 1;
  saved.centre_modes[0] = 2;
  saved.centre[0] = 123;
  EEPROM.put(0, saved);
  EEPROM.commit();
  hw::settings_load();
  assert(hw::settings().edition == cfg::SETTINGS_EDITION);
  assert(hw::settings().brightness == 77);
  for (auto mode : hw::settings().centre_modes) assert(mode == mp::ENC_LAYER_DEFAULT);
  for (auto byte : hw::settings().centre) assert(byte == 0);
  const unsigned commits = EEPROM.commits;
  hw::settings_load();
  assert(EEPROM.commits == commits);
  saved.version = 5;
  EEPROM.put(0, saved);
  EEPROM.commit();
  hw::settings_load();
  assert(hw::settings().brightness == cfg::BRIGHTNESS_DEFAULT);
}

static void test_diagnostics_do_not_block_input() {
  for (int& pin : mock_pins) pin = 1;
  mock_adc[26] = mock_adc[27] = 2048;
  Host host;
  mp::Engine engine(host);
  Centre centre;
  centre.begin(engine, hw::settings().centre);
  mock_now = 100;
#ifdef TEST_JOYSTICK
  assert(centre.console("joy"));
  engine.set_base_layer(1);  // arrows; stick click produces Enter
#else
  assert(centre.console("enc"));
#endif
  assert(mock_now == 100 && mock_delays == 0);
  Serial.space = 0;
  const auto output = Serial.output;
  mock_pins[22] = LOW;
  for (uint32_t now = 108; now <= 140; now += 8) centre.task(now);
  assert(Serial.output == output);  // a full serial buffer cannot stall the task
#ifdef TEST_JOYSTICK
  assert(engine.hid().has_key(mp::hid::ENTER));
#else
  assert(engine.hid().consumer == mp::cc::MUTE);
#endif
  Serial.space = 128;
  centre.task(208);
  assert(Serial.output.size() > output.size());
  const auto after = Serial.output;
  centre.task(5100);
  centre.task(5200);
  assert(Serial.output == after);
  assert(mock_delays == 0);
#ifdef TEST_JOYSTICK
  mock_adc[26] = 100;
  mock_adc[27] = 4000;
  centre.task(5210);
  mp::JoyCal cal;
  memcpy(&cal, hw::settings().centre, sizeof cal);
  assert(cal.min_x == 100 && cal.max_y == 4000);
  const unsigned commits = EEPROM.commits;
  hw::settings_task(6000);
  assert(EEPROM.commits == commits);
  hw::settings_task(8300);
  assert(EEPROM.commits == commits + 1);
  hw::settings_load();
  memcpy(&cal, hw::settings().centre, sizeof cal);
  assert(cal.min_x == 100 && cal.max_y == 4000);
#endif
}

static void test_settings_save_waits_after_a_late_stamp() {
  const unsigned commits = EEPROM.commits;
  hw::settings_touch(1001);  // a callback's millis() can be newer than the loop's `now`
  hw::settings_task(1000);
  assert(EEPROM.commits == commits);
  hw::settings_task(4001);
  assert(EEPROM.commits == commits);
  hw::settings_task(4002);
  assert(EEPROM.commits == commits + 1);
}

#ifndef TEST_JOYSTICK
static void turn_knob(int leading_pin, int trailing_pin) {  // one detent from rest, as four edges
  for (int pin : {leading_pin, trailing_pin, leading_pin, trailing_pin}) {
    mock_pins[pin] ^= 1;
    mock_isr[pin]();
  }
}

static void test_knob_turns_reverses_and_keeps_its_preset() {
  for (int& pin : mock_pins) pin = 1;
  hw::settings_load();  // drop what earlier groups left in the settings
  Host host;
  mp::Engine engine(host);
  Centre centre;
  centre.begin(engine, hw::settings().centre);
  turn_knob(cfg::ENC_A_PIN, cfg::ENC_B_PIN);  // CLK first = clockwise
  centre.task(100);
  assert(engine.encoder_position() == 1 && engine.hid().consumer == mp::cc::VOL_UP);
  turn_knob(cfg::ENC_B_PIN, cfg::ENC_A_PIN);
  centre.task(120);
  assert(engine.encoder_position() == 0);

  assert(!centre.on_sys(mp::SYS_BRIGHT_UP, 130));
  assert(centre.on_sys(mp::SYS_ENC_REVERSE, 130));
  assert(hw::settings().centre[0] == 1 && centre.reversed());
  turn_knob(cfg::ENC_A_PIN, cfg::ENC_B_PIN);
  centre.task(140);
  assert(engine.encoder_position() == -1 && engine.hid().consumer == mp::cc::VOL_DOWN);

  const uint8_t base = engine.base_layer();
  const uint8_t* modes = hw::settings().centre_modes;
  centre.on_mode(mp::ENC_CYCLE, 150);
  assert(modes[base] == engine.enc_preset(base) && modes[base] != mp::ENC_LAYER_DEFAULT);
  centre.on_mode(3, 160);  // not the next one in the cycle
  assert(modes[base] == 3);
  mp::Engine rebooted_engine(host);
  Centre rebooted;
  rebooted.begin(rebooted_engine, hw::settings().centre);
  assert(strcmp(rebooted_engine.encoder_binding().label, "ARROWS") == 0 && rebooted.reversed());
  hw::settings_load();  // nothing was committed: back to the stored settings
}

static void test_knob_switch_is_debounced() {
  for (int& pin : mock_pins) pin = 1;
  Host host;
  mp::Engine engine(host);
  Centre centre;
  centre.begin(engine, hw::settings().centre);
  const auto muted = [&] { return engine.hid().consumer == mp::cc::MUTE; };
  mock_pins[cfg::ENC_SW_PIN] = LOW;
  for (uint32_t now = 100; now <= 104; now += 2) centre.task(now);  // 3 samples: not yet
  assert(!muted() && !engine.encoder_pressed());
  centre.task(105);  // 1 ms after the last sample: not a sample
  assert(!muted());
  mock_pins[cfg::ENC_SW_PIN] = 1;
  centre.task(106);  // a bounce restarts the count
  mock_pins[cfg::ENC_SW_PIN] = LOW;
  for (uint32_t now = 108; now <= 112; now += 2) centre.task(now);
  assert(!muted());
  centre.task(114);  // the 4th equal sample
  assert(muted() && engine.encoder_pressed());
  mock_pins[cfg::ENC_SW_PIN] = 1;
  for (uint32_t now = 116; now <= 120; now += 2) centre.task(now);
  assert(muted());  // the release is debounced too
  centre.task(122);
  assert(!muted() && !engine.encoder_pressed());
}
#endif

static void test_host_link_round_trip() {
  hw::host_hid_begin();
  Adafruit_USBD_HID* vendor = mock_hids.back();
  assert(vendor->vendor && vendor->set_report);
  uint8_t pkt[mp::HOST_PACKET] = {mp::PKT_HELLO, mp::PROTO_VERSION, 'p', 'c'};
  vendor->set_report(0, HID_REPORT_TYPE_OUTPUT, pkt, sizeof pkt);  // from TinyUSB's interrupt
  uint8_t in[mp::HOST_PACKET];
  uint16_t len = 0;
  assert(hw::host_hid_read(in, len) && len == mp::HOST_PACKET);
  assert(!hw::host_hid_read(in, len));
  mp::HostProto proto;
  proto.set_identity(cfg::SETTINGS_EDITION, mp::NUM_LAYERS, cfg::USB_PRODUCT_NAME);
  assert(proto.on_packet(in, len, 0) && proto.linked());
  uint8_t out[mp::HOST_PACKET];
  assert(proto.next_tx(out));
  mock_vendor_reports.clear();
  mock_vendor_ready = true;
  assert(hw::host_hid_write(out));
  assert(!hw::host_hid_write(out));  // endpoint busy until the transfer completes
  assert(mock_vendor_reports.size() == 1 && mock_vendor_reports[0][0] == mp::PKT_HELLO_ACK);
  assert(mock_vendor_reports[0][2] == cfg::SETTINGS_EDITION);
  mock_vendor_ready = true;
  TinyUSBDevice.asleep = true;
  assert(!hw::host_hid_write(out));  // nothing is sent to a sleeping host
  TinyUSBDevice.asleep = false;

  for (int i = 0; i < 6; ++i) vendor->set_report(0, HID_REPORT_TYPE_OUTPUT, pkt, 8);  // short packets, ring full
  int n = 0;
  while (hw::host_hid_read(in, len)) {
    assert(len == 8);
    ++n;
  }
  assert(n == 3);  // a full ring drops packets instead of overwriting one being read
}

int main() {
  test_usb_preserves_press_and_release();
  test_usb_retries_failed_reports();
  test_usb_resends_after_remount();
  test_usb_overflow_eventually_releases();
  test_motion_does_not_pause_generated_keys();
  test_suspended_click_requests_wake();
  test_settings_isolate_editions();
  test_diagnostics_do_not_block_input();
  test_settings_save_waits_after_a_late_stamp();
#ifdef TEST_JOYSTICK
  test_host_link_round_trip();
  puts("10 hardware/control regression groups passed");
#else
  test_knob_turns_reverses_and_keeps_its_preset();
  test_knob_switch_is_debounced();
  test_host_link_round_trip();
  puts("12 hardware/control regression groups passed");
#endif
}
