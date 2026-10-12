// Compile production hardware/control code against narrow Arduino/USB/EEPROM mocks.
#include <cassert>
#include <cstdio>
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <EEPROM.h>
#include "config.h"
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

int main() {
  test_usb_preserves_press_and_release();
  test_usb_retries_failed_reports();
  test_usb_resends_after_remount();
  test_usb_overflow_eventually_releases();
  test_motion_does_not_pause_generated_keys();
  test_suspended_click_requests_wake();
  test_settings_isolate_editions();
  test_diagnostics_do_not_block_input();
  puts("8 hardware/control regression groups passed");
}
