// Compile production hardware/control code against narrow Arduino/USB/EEPROM mocks.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <EEPROM.h>
#include "app/console_line.h"
#include "config.h"
#include "core/host_proto.h"
#include "hw/host_hid.h"
#include "hw/matrix.h"
#include "hw/settings.h"
#include "hw/usb_hid.h"
#include "ui/dirty_rows.h"
#include "ui/host_fmt.h"
#ifdef TEST_JOYSTICK
#include "stick.h"
using Centre = JoystickCentre;
#else
#include "knob.h"
using Centre = KnobCentre;
#endif

static int g_activity_calls = 0, g_hit_calls = 0;  // what the centre control reports to the app
namespace app {
void activity(uint32_t) { ++g_activity_calls; }
void hit(uint32_t) { ++g_hit_calls; }
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

// Must run first: usb_hid has not seen a mount yet (the mock starts connected, so every later group remounts).
static void test_usb_drops_keys_pressed_before_the_first_mount() {
  TinyUSBDevice.connected = false;
  hw::usb_task();
  mp::HidState tapped, held;
  tapped.mods = mp::MOD_LCTRL;
  tapped.keys[0] = mp::hid::Z;
  held.keys[0] = mp::hid::B;
  hw::usb_set_keyboard(tapped);  // a pad with power but no host: Ctrl+Z tapped twice, then B held
  hw::usb_set_keyboard({});
  hw::usb_set_keyboard(tapped);
  hw::usb_set_keyboard({});
  hw::usb_set_keyboard(held);
  hw::usb_task();
  assert(mock_reports.empty());
  TinyUSBDevice.connected = true;
  drain();
  assert(mock_reports.size() == 3);  // only what is held now; the taps are not typed into the login screen
  assert(mock_reports[0].id == 1 && mock_reports[0].value == 0 && mock_reports[0].keys[0] == mp::hid::B);
  assert(mock_reports[1].id == 3 && mock_reports[1].value == 0);
  assert(mock_reports[2].id == 2 && mock_reports[2].value == 0);
  hw::usb_set_keyboard({});
  drain();
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

static void test_usb_keeps_a_repeated_tap_while_busy() {
  drain();
  mock_reports.clear();
  mock_ready = false;
  mp::HidState a;
  a.keys[0] = mp::hid::A;
  hw::usb_set_keyboard(a);
  hw::usb_set_keyboard({});
  hw::usb_set_keyboard(a);
  hw::usb_set_keyboard({});
  drain();
  assert(mock_reports.size() == 4);
  for (int i = 0; i < 4; ++i) assert(mock_reports[i].id == 1 && mock_reports[i].keys[0] == (i % 2 ? 0 : mp::hid::A));
}

static void test_usb_mount_publishes_a_held_stick_click() {
  drain();
  TinyUSBDevice.connected = false;
  hw::usb_task();
  hw::usb_set_extra_buttons(mp::MB_LEFT);
  mock_reports.clear();
  TinyUSBDevice.connected = true;
  drain();
  assert(mock_reports.size() == 3);  // keyboard, consumer, then the mouse report with the button
  assert(mock_reports[2].id == 2 && mock_reports[2].value == mp::MB_LEFT);
  hw::usb_set_extra_buttons(0);
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

static void test_motion_keeps_the_held_button() {
  drain();
  hw::usb_set_extra_buttons(mp::MB_LEFT);  // stick click held
  drain();
  mock_reports.clear();
  hw::usb_add_mouse(5, 0, 0, 0);
  drain();
  assert(mock_reports.size() == 1);
  assert(mock_reports[0].id == 2 && mock_reports[0].value == mp::MB_LEFT && mock_reports[0].dx == 5);
  hw::usb_set_extra_buttons(0);
  mp::HidState held;  // an MB() key held
  held.mouse_buttons = mp::MB_RIGHT;
  hw::usb_set_keyboard(held);
  drain();
  mock_reports.clear();
  hw::usb_add_mouse(0, 3, 0, 0);
  drain();
  assert(mock_reports.size() == 1);
  assert(mock_reports[0].id == 2 && mock_reports[0].value == mp::MB_RIGHT && mock_reports[0].dy == 3);
  hw::usb_set_keyboard({});
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

static void test_input_is_dropped_while_the_host_refuses_wake() {
  drain();
  mock_reports.clear();
  mp::HidState tapped, held;
  tapped.mods = mp::MOD_LCTRL;
  tapped.keys[0] = mp::hid::Z;
  held.keys[0] = mp::hid::B;
  TinyUSBDevice.asleep = true;
  TinyUSBDevice.wake_ok = false;  // e.g. Linux: wake-up is armed for boot keyboards only
  assert(!hw::usb_wake_refused());
  hw::usb_set_keyboard(tapped);
  hw::usb_task();
  assert(hw::usb_wake_refused());
  hw::usb_set_keyboard({});
  hw::usb_task();
  TinyUSBDevice.asleep = false;  // woken some other way, hours later
  drain();
  assert(mock_reports.empty());  // Ctrl+Z is not typed into the lock screen
  assert(!hw::usb_wake_refused());

  TinyUSBDevice.asleep = true;
  hw::usb_set_keyboard(tapped);
  hw::usb_task();
  hw::usb_set_keyboard({});
  hw::usb_task();
  hw::usb_set_keyboard(held);
  hw::usb_task();
  TinyUSBDevice.asleep = false;
  drain();
  assert(mock_reports.size() == 1);  // only what is held at resume
  assert(mock_reports[0].id == 1 && mock_reports[0].value == 0 && mock_reports[0].keys[0] == mp::hid::B);
  hw::usb_set_keyboard({});
  drain();
  assert(mock_reports.size() == 2 && mock_reports[1].keys[0] == 0);

  hw::usb_set_keyboard(held);
  drain();
  TinyUSBDevice.asleep = true;  // suspended with B down, released while asleep
  hw::usb_set_keyboard({});
  hw::usb_task();
  TinyUSBDevice.connected = false;
  hw::usb_task();
  assert(!hw::usb_wake_refused());  // unplugged: the no-host rule applies instead
  TinyUSBDevice.connected = true;
  TinyUSBDevice.asleep = false;
  TinyUSBDevice.wake_ok = true;
  drain();
}

static void test_usb_finishes_the_resync_after_a_refused_wake() {
  drain();
  TinyUSBDevice.connected = false;
  hw::usb_task();
  mock_reports.clear();
  TinyUSBDevice.connected = true;
  mock_ready = true;
  hw::usb_task();  // the keyboard report of the resync, then the host suspends
  assert(mock_reports.size() == 1 && mock_reports[0].id == 1);
  TinyUSBDevice.asleep = true;
  TinyUSBDevice.wake_ok = false;
  hw::usb_task();
  TinyUSBDevice.asleep = false;
  TinyUSBDevice.wake_ok = true;
  mock_ready = true;
  assert(!hw::usb_keyboard_delivered());  // the host has not seen the consumer and mouse state yet
  drain();  // consumer and mouse still go out, so generated taps are not held back for good
  assert(mock_reports.size() == 3 && mock_reports[1].id == 3 && mock_reports[2].id == 2);
}

static void test_wheel_step_is_sent_once_and_dropped_while_suspended() {
  drain();
  mock_reports.clear();
  hw::usb_add_mouse(0, 0, -1, 0);  // one knob detent on a WHEEL binding
  drain();
  assert(mock_reports.size() == 1);
  assert(mock_reports[0].wheel == -1 && mock_reports[0].dx == 0 && mock_reports[0].dy == 0 && mock_reports[0].pan == 0);
  drain();
  assert(mock_reports.size() == 1);  // not replayed
  TinyUSBDevice.asleep = true;
  hw::usb_add_mouse(0, 0, 1, 0);
  hw::usb_add_mouse(5, 5, 0, 1);  // and the stick, nudged
  hw::usb_task();
  TinyUSBDevice.asleep = false;
  mock_reports.clear();
  drain();
  assert(mock_reports.empty());  // a detent turned while the host sleeps does not scroll on wake-up
}

static void test_usb_reports_the_lock_leds() {
  TinyUSBDevice.connected = false;  // as at power-up: nothing to re-enumerate, no delay()
  hw::usb_begin();
  TinyUSBDevice.connected = true;
  Adafruit_USBD_HID* keyboard = mock_hids.back();
  assert(!keyboard->vendor && keyboard->set_report);
  const uint8_t num = 0x01, caps = 0x02, prefixed_scroll[] = {1, 0x04};
  keyboard->set_report(1, HID_REPORT_TYPE_OUTPUT, &caps, 1);
  assert(hw::usb_leds() == caps);
  keyboard->set_report(0, HID_REPORT_TYPE_OUTPUT, &num, 1);  // a host that sends no report id
  assert(hw::usb_leds() == num);
  keyboard->set_report(0, HID_REPORT_TYPE_OUTPUT, prefixed_scroll, 2);  // the id as the first byte
  assert(hw::usb_leds() == 0x04);
  keyboard->set_report(1, HID_REPORT_TYPE_OUTPUT + 1, &caps, 1);  // a feature report is not the LEDs
  keyboard->set_report(1, HID_REPORT_TYPE_OUTPUT, &caps, 0);
  assert(hw::usb_leds() == 0x04);
}

static void test_button_report_in_flight_is_not_delivered() {
  drain();
  mock_ready = true;
  hw::usb_set_extra_buttons(mp::MB_LEFT);
  hw::usb_task();
  assert(!mock_ready && !hw::usb_keyboard_delivered());  // sent, the host has not taken it yet
  mock_ready = true;
  assert(hw::usb_keyboard_delivered());
  hw::usb_set_extra_buttons(0);
  drain();
}

static int send_pending_motion() {  // until the host has it all; returns the dx it got
  const size_t first = mock_reports.size();
  for (size_t sent = first - 1; sent != mock_reports.size();) {
    sent = mock_reports.size();
    mock_ready = true;
    hw::usb_task();
  }
  int dx = 0;
  for (size_t i = first; i < mock_reports.size(); i++) {
    assert(mock_reports[i].dx > 0 && mock_reports[i].dx <= 127);
    dx += mock_reports[i].dx;
  }
  return dx;
}

static void test_motion_waits_for_a_busy_endpoint_but_not_without_limit() {
  drain();
  mock_reports.clear();
  hw::usb_add_mouse(100, 0, 0, 0);  // two stick samples before the endpoint is free
  hw::usb_add_mouse(100, 0, 0, 0);
  assert(send_pending_motion() == 200);  // more than one report holds: the rest follows

  mock_ready = false;  // mounted and awake, but the host stopped polling (frozen, a paused VM)
  for (int i = 0; i < 4000; i++) {
    hw::usb_add_mouse(9, 0, 0, 0);
    hw::usb_task();
  }
  assert(mock_reports.size() == 2);
  assert(send_pending_motion() == 254);  // two reports' worth: no pointer jump, no wrap to the left
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

static void test_settings_flush_saves_before_a_reboot() {
  const unsigned commits = EEPROM.commits;
  hw::settings_flush();  // nothing changed: no write
  assert(EEPROM.commits == commits);
  const uint8_t brightness = hw::settings().brightness;
  hw::settings().brightness = uint8_t(brightness + 1);
  hw::settings_touch(9000);
  hw::settings_flush();  // inside the save delay
  assert(EEPROM.commits == commits + 1);
  hw::settings_task(20000);
  assert(EEPROM.commits == commits + 1);  // already saved: not written twice
  hw::settings_load();
  assert(hw::settings().brightness == uint8_t(brightness + 1));
  hw::settings().brightness = brightness;  // back to what the groups that follow expect
  hw::settings_touch(20000);
  hw::settings_flush();
}

#ifndef TEST_JOYSTICK
static void turn_knob(int leading_pin, int trailing_pin) {  // one detent from rest, as four edges
  for (int pin : {leading_pin, trailing_pin, leading_pin, trailing_pin}) {
    mock_pins[pin] ^= 1;
    mock_isr[pin]();
  }
}

// What one clockwise turn_knob gives with the builder's switches in config.h, so flipping one keeps the suite green.
constexpr int CW_DETENT = (cfg::ENC_REVERSE ? -1 : 1) * (cfg::ENC_STEPS_PER_DETENT == 2 ? 2 : 1);
constexpr uint16_t CW_USAGE = cfg::ENC_REVERSE ? mp::cc::VOL_DOWN : mp::cc::VOL_UP;
constexpr uint16_t CCW_USAGE = cfg::ENC_REVERSE ? mp::cc::VOL_UP : mp::cc::VOL_DOWN;

static void test_enc_diagnostic_reports_the_pins() {
  for (int& pin : mock_pins) pin = 1;
  Host host;
  mp::Engine engine(host);
  Centre centre;
  centre.begin(engine, hw::settings().centre);
  assert(!centre.console("joy"));  // unknown commands fall through to help
  assert(!centre.console("encoder"));
  turn_knob(cfg::ENC_A_PIN, cfg::ENC_B_PIN);
  Serial.space = 128;
  Serial.output.clear();
  centre.task(900);
  assert(Serial.output.empty());  // nothing until `enc`
  char info[64], line[64];
  snprintf(info, sizeof info, "knob VOLUME pos=%d reversed=%d\n", CW_DETENT, cfg::ENC_REVERSE);
  snprintf(line, sizeof line, "CLK=0 DT=1 SW=1  pos=%d\n", CW_DETENT);
  centre.info();
  assert(Serial.output == info);
  mock_pins[cfg::ENC_A_PIN] = 0;  // CLK low, DT high, switch pressed; no ISR, so pos stays
  mock_pins[cfg::ENC_SW_PIN] = LOW;
  mock_now = 1000;
  assert(centre.console("enc"));
  Serial.output.clear();
  centre.task(1000);  // the first line at once
  assert(Serial.output == line);
  centre.task(1049);  // then one every 50 ms
  assert(Serial.output == line);
  centre.task(1050);
  assert(Serial.output == std::string(line) + line);
}

static void test_knob_turns_reverses_and_keeps_its_preset() {
  for (int& pin : mock_pins) pin = 1;
  hw::settings_load();  // drop what earlier groups left in the settings
  Host host;
  mp::Engine engine(host);
  Centre centre;
  centre.begin(engine, hw::settings().centre);
  const int activity = g_activity_calls, hits = g_hit_calls;
  turn_knob(cfg::ENC_A_PIN, cfg::ENC_B_PIN);  // CLK first = clockwise
  centre.task(100);
  assert(engine.encoder_position() == CW_DETENT && engine.hid().consumer == CW_USAGE);
  assert(g_activity_calls > activity && g_hit_calls > hits);  // a detent wakes the displays, flashes the LEDs
  turn_knob(cfg::ENC_B_PIN, cfg::ENC_A_PIN);
  centre.task(120);
  assert(engine.encoder_position() == 0);
  for (int i = 0; i < 3; i++) turn_knob(cfg::ENC_A_PIN, cfg::ENC_B_PIN);  // a fast turn: three detents before one pass
  centre.task(122);
  assert(engine.encoder_position() == 3 * CW_DETENT);
  for (int i = 0; i < 3; i++) turn_knob(cfg::ENC_B_PIN, cfg::ENC_A_PIN);
  centre.task(124);
  assert(engine.encoder_position() == 0);

  assert(!centre.on_sys(mp::SYS_BRIGHT_UP, 130));
  assert(centre.on_sys(mp::SYS_ENC_REVERSE, 130));
  assert(hw::settings().centre[0] == 1 && centre.reversed() == !cfg::ENC_REVERSE);
  unsigned commits = EEPROM.commits;
  hw::settings_task(3130);  // still inside the save delay
  assert(EEPROM.commits == commits);
  hw::settings_task(3131);
  assert(EEPROM.commits == commits + 1);  // the direction reaches flash
  engine.clear_queued_taps();  // or the next tap out is one still queued from the turns above
  turn_knob(cfg::ENC_A_PIN, cfg::ENC_B_PIN);
  centre.task(140);
  assert(engine.encoder_position() == -CW_DETENT && engine.hid().consumer == CCW_USAGE);
  assert(centre.on_sys(mp::SYS_ENC_REVERSE, 141));  // a second REV turns it back
  assert(hw::settings().centre[0] == 0 && centre.reversed() == cfg::ENC_REVERSE);
  assert(centre.on_sys(mp::SYS_ENC_REVERSE, 142));  // and the rest of the group runs reversed

  engine.set_base_layer(1);  // the preset belongs to the base layer, not to layer 0
  const uint8_t base = engine.base_layer();
  const uint8_t* modes = hw::settings().centre_modes;
  centre.on_mode(mp::ENC_CYCLE, 150);
  assert(modes[base] == engine.enc_preset(base) && modes[base] != mp::ENC_LAYER_DEFAULT);
  centre.on_mode(3, 160);  // not the next one in the cycle
  assert(modes[base] == 3);
  ui::UiState u;  // the displays name the preset the knob now sends, not the layer's own knob
  centre.fill_ui(u);
  assert(u.centre_encoder && strcmp(u.enc_label, "ARROWS") == 0);
  assert(u.enc_pos == engine.encoder_position() && !u.enc_press);
  commits = EEPROM.commits;
  hw::settings_task(3161);
  assert(EEPROM.commits == commits + 1);  // so does the preset
  hw::settings_load();  // a reboot reads flash, not RAM
  mp::Engine rebooted_engine(host);
  Centre rebooted;
  rebooted.begin(rebooted_engine, hw::settings().centre);
  assert(base == 1 && modes[0] == mp::ENC_LAYER_DEFAULT);
  assert(strcmp(rebooted_engine.encoder_binding().label, "VOLUME") == 0);  // layer 0 kept its own knob
  rebooted_engine.set_base_layer(base);
  assert(strcmp(rebooted_engine.encoder_binding().label, "ARROWS") == 0);
  assert(rebooted.reversed() == !cfg::ENC_REVERSE);
  hw::settings().centre[0] = 0;  // back to the defaults for the groups that follow
  hw::settings().centre_modes[base] = mp::ENC_LAYER_DEFAULT;
  hw::settings_touch(4000);
  hw::settings_task(7001);
}

static void test_knob_switch_is_debounced() {
  for (int& pin : mock_pins) pin = 1;
  Host host;
  mp::Engine engine(host);
  Centre centre;
  for (int& mode : mock_pin_mode) mode = 0;  // an earlier group's begin() must not satisfy the asserts
  for (int& mode : mock_isr_mode) mode = 0;
  centre.begin(engine, hw::settings().centre);
  for (int pin : {cfg::ENC_A_PIN, cfg::ENC_B_PIN, cfg::ENC_SW_PIN}) assert(mock_pin_mode[pin] == INPUT_PULLUP);
  assert(mock_isr_mode[cfg::ENC_A_PIN] == CHANGE && mock_isr_mode[cfg::ENC_B_PIN] == CHANGE);  // all four edges
  const auto muted = [&] { return engine.hid().consumer == mp::cc::MUTE; };
  const int activity = g_activity_calls, hits = g_hit_calls;
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
  ui::UiState pressed;
  centre.fill_ui(pressed);
  assert(pressed.enc_press);
  assert(g_activity_calls == activity + 1 && g_hit_calls == hits + 1);
  mock_pins[cfg::ENC_SW_PIN] = 1;
  for (uint32_t now = 116; now <= 120; now += 2) centre.task(now);
  assert(muted());  // the release is debounced too
  centre.task(122);
  assert(!muted() && !engine.encoder_pressed());
  assert(g_activity_calls == activity + 2 && g_hit_calls == hits + 1);  // a release is activity, not a hit
}

// Forwards like app.cpp's Host, which these tests do not compile: this pins the keymap's FN cells, not that glue.
struct KnobHost : Host {
  KnobCentre* centre = nullptr;
  std::vector<mp::SysCmd> sys;
  void on_sys(mp::SysCmd cmd) override {
    sys.push_back(cmd);
    centre->on_sys(cmd, mock_now);
  }
  void on_centre(uint8_t mode) override { centre->on_mode(mode, mock_now); }
};

static void test_keymap_fn_keys_reach_the_knob() {
  for (int& pin : mock_pins) pin = 1;
  KnobHost host;
  mp::Engine engine(host);
  Centre centre;
  centre.begin(engine, hw::settings().centre);
  host.centre = &centre;
  const uint8_t base = engine.base_layer();
  engine.process({3, 0, true, 0});  // LAYER
  engine.tick(300);                 // held past the tap-hold term: FN
  assert(engine.top_layer() == mp::FN_LAYER);
  engine.process({1, 1, true, 310});  // REV
  engine.process({1, 1, false, 320});
  assert(centre.reversed() == !cfg::ENC_REVERSE);
  engine.process({1, 0, true, 330});  // KNOB
  engine.process({1, 0, false, 340});
  assert(hw::settings().centre_modes[base] != mp::ENC_LAYER_DEFAULT);
  hw::settings_load();  // nothing was committed: back to the stored settings

  turn_knob(cfg::ENC_A_PIN, cfg::ENC_B_PIN);
  centre.task(350);
  turn_knob(cfg::ENC_B_PIN, cfg::ENC_A_PIN);
  centre.task(352);
  mock_pins[cfg::ENC_SW_PIN] = LOW;
  for (uint32_t now = 354; now <= 360; now += 2) centre.task(now);
  mock_pins[cfg::ENC_SW_PIN] = 1;
  for (uint32_t now = 362; now <= 368; now += 2) centre.task(now);
  // with FN held the knob dims and blanks; a press must never reach SYS_BOOTLOADER
  const mp::SysCmd cw = cfg::ENC_REVERSE ? mp::SYS_BRIGHT_DOWN : mp::SYS_BRIGHT_UP;
  const mp::SysCmd ccw = cfg::ENC_REVERSE ? mp::SYS_BRIGHT_UP : mp::SYS_BRIGHT_DOWN;
  const size_t steps = CW_DETENT < 0 ? -CW_DETENT : CW_DETENT;
  std::vector<mp::SysCmd> expected = {mp::SYS_ENC_REVERSE};
  expected.insert(expected.end(), steps, cw);
  expected.insert(expected.end(), steps, ccw);
  expected.push_back(mp::SYS_DISPLAYS_TOGGLE);
  assert(host.sys == expected);
}
#endif

#ifdef TEST_JOYSTICK
// Forwards like app.cpp's Host, which these tests do not compile.
struct StickHost : Host {
  JoystickCentre* centre = nullptr;
  void on_centre(uint8_t mode) override { centre->on_mode(mode, mock_now); }
};

static uint8_t last_mouse_buttons() {  // of the reports sent since mock_reports was cleared; 0xFF = none
  for (auto r = mock_reports.rbegin(); r != mock_reports.rend(); ++r)
    if (r->id == 2) return uint8_t(r->value);
  return 0xFF;
}

static void test_stick_mode_cycle_and_click() {
  using mp::JoyMode;
  for (int& pin : mock_pins) pin = 1;
  mock_adc[cfg::JOY_X_PIN] = mock_adc[cfg::JOY_Y_PIN] = 2048;
  hw::settings_load();
  StickHost host;
  mp::Engine engine(host);
  Centre centre;
  centre.begin(engine, hw::settings().centre);
  host.centre = &centre;
  const uint8_t base = engine.base_layer();
  const uint8_t* modes = hw::settings().centre_modes;
  const auto shown_mode = [&] {
    ui::UiState u;
    centre.fill_ui(u);
    return JoyMode(u.joy_mode);
  };
  assert(base == 0 && shown_mode() == JoyMode::Mouse);  // MEDIA: STICK(Mouse)

  uint32_t now = 0;
  for (JoyMode next : {JoyMode::Scroll, JoyMode::Arrows, JoyMode::Off, JoyMode::Mouse}) {  // FN + JOY
    engine.process({3, 0, true, now});  // LAYER
    engine.tick(now + 300);             // held past the tap-hold term: FN
    assert(engine.top_layer() == mp::FN_LAYER);
    engine.process({1, 0, true, now + 310});  // JOY
    engine.process({1, 0, false, now + 320});
    assert(modes[base] == uint8_t(next));
    engine.process({3, 0, false, now + 330});
    assert(shown_mode() == next);  // the saved mode wins over the layer's own STICK()
    now += 400;
  }
  centre.on_mode(uint8_t(JoyMode::Scroll), now);
  hw::settings_flush();
  hw::settings_load();  // a reboot reads flash, not RAM
  assert(shown_mode() == JoyMode::Scroll);

  centre.on_mode(uint8_t(JoyMode::Mouse), now);
  const auto run_tasks = [&](int n) {
    for (int i = 0; i < n; i++) centre.task(now += cfg::JOY_PERIOD_MS);
    drain();
  };
  drain();
  mock_reports.clear();
  const int hits = g_hit_calls;
  mock_pins[cfg::JOY_SW_PIN] = LOW;
  run_tasks(2);
  assert(last_mouse_buttons() == 0xFF);  // two samples: still bouncing
  run_tasks(1);
  assert(last_mouse_buttons() == mp::MB_LEFT);
  assert(g_hit_calls == hits + 1);  // a click flashes the LEDs
  mock_pins[cfg::JOY_SW_PIN] = 1;
  run_tasks(3);
  assert(last_mouse_buttons() == 0);
  assert(g_hit_calls == hits + 1);  // the release does not

  mock_pins[cfg::JOY_SW_PIN] = LOW;
  run_tasks(1);
  for (int i = 0; i < 5; i++) centre.task(now);  // the loop comes round many times per period:
  centre.task(now + cfg::JOY_PERIOD_MS - 1);     // those passes are not samples
  drain();
  assert(g_hit_calls == hits + 1 && last_mouse_buttons() == 0);
  for (int level : {1, LOW, 1, LOW, 1}) {  // noise: never three low samples in a row
    mock_pins[cfg::JOY_SW_PIN] = level;
    run_tasks(1);
  }
  assert(g_hit_calls == hits + 1 && last_mouse_buttons() == 0);

  mock_pins[cfg::JOY_SW_PIN] = LOW;  // a drag, and the mode changes under it
  run_tasks(3);
  assert(last_mouse_buttons() == mp::MB_LEFT);
  centre.on_mode(uint8_t(JoyMode::Arrows), now);
  run_tasks(3);
  assert(last_mouse_buttons() == mp::MB_LEFT && !engine.hid().has_key(mp::hid::ENTER));  // still the drag
  mock_pins[cfg::JOY_SW_PIN] = 1;
  run_tasks(3);
  assert(last_mouse_buttons() == 0);
  mock_pins[cfg::JOY_SW_PIN] = LOW;  // the next click has the new meaning
  run_tasks(3);
  assert(last_mouse_buttons() == 0 && engine.hid().has_key(mp::hid::ENTER));
  mock_pins[cfg::JOY_SW_PIN] = 1;
  run_tasks(3);
  assert(!engine.hid().has_key(mp::hid::ENTER));

  centre.on_mode(uint8_t(JoyMode::Scroll), now);
  mock_pins[cfg::JOY_SW_PIN] = LOW;
  run_tasks(3);
  assert(last_mouse_buttons() == mp::MB_MIDDLE);
  mock_pins[cfg::JOY_SW_PIN] = 1;
  run_tasks(3);

  mock_reports.clear();
  mock_adc[cfg::JOY_Y_PIN] = 4000;  // pushed along Y only: the wheel, not the pan
  for (int i = 0; i < 100; i++) run_tasks(1);
  bool wheel = false;
  for (const auto& r : mock_reports) {
    assert(r.id == 2 && r.pan == 0 && r.dx == 0 && r.dy == 0);
    wheel = wheel || r.wheel != 0;
  }
  assert(wheel);

  mock_adc[cfg::JOY_Y_PIN] = 2048;
  hw::settings_load();  // drop the travel learned above, then put the layer's own mode back in flash
  hw::settings().centre_modes[base] = uint8_t(JoyMode::Inherit);
  hw::settings_touch(now);
  hw::settings_flush();
}

static void boot_stick_at_rest(Centre& centre, mp::Engine& engine) {  // default travel, centre 2048
  for (int& pin : mock_pins) pin = 1;
  mock_adc[cfg::JOY_X_PIN] = mock_adc[cfg::JOY_Y_PIN] = 2048;
  hw::settings_load();
  memset(hw::settings().centre, 0, hw::CENTRE_BYTES);  // as on a first boot
  centre.begin(engine, hw::settings().centre);
  hw::settings_flush();
}

static void test_stick_saves_the_learned_range_not_the_centre() {
  Host host;
  mp::Engine engine(host);
  Centre first_boot;
  const unsigned before = EEPROM.commits;
  boot_stick_at_rest(first_boot, engine);
  assert(EEPROM.commits == before + 1);  // the default travel is stored once
  const unsigned commits = EEPROM.commits;

  uint32_t now = mock_now = 10000;
  mock_adc[cfg::JOY_X_PIN] = 2051;  // ADC noise: the rest position differs at every power-up
  mock_adc[cfg::JOY_Y_PIN] = 2045;
  Centre centre;
  centre.begin(engine, hw::settings().centre);
  hw::settings_task(now += 3001);
  assert(EEPROM.commits == commits);  // it is sampled again at the next boot: not worth a flash erase
  mock_adc[cfg::JOY_X_PIN] = 2060;
  assert(centre.on_sys(mp::SYS_JOY_CALIBRATE, now));
  hw::settings_task(now += 3001);
  assert(EEPROM.commits == commits);

  mock_adc[cfg::JOY_X_PIN] = 4000;  // past the default travel
  centre.task(now);
  hw::settings_task(now + 3000);
  assert(EEPROM.commits == commits);
  hw::settings_task(now + 3001);
  assert(EEPROM.commits == commits + 1);
  mp::JoyCal cal;
  hw::settings_load();
  memcpy(&cal, hw::settings().centre, sizeof cal);
  assert(cal.max_x == 4000 && cal.cx == 2060);  // written whole when it is written
  mock_adc[cfg::JOY_X_PIN] = 2048;
  drain();  // the pointer motion of that one pass
}

static void test_stick_motion_wakes_and_does_not_starve_the_save() {
  using mp::JoyMode;
  Host host;
  mp::Engine engine(host);
  Centre centre;
  boot_stick_at_rest(centre, engine);
  const uint8_t base = engine.base_layer();
  uint32_t now = 20000;
  centre.on_mode(uint8_t(JoyMode::Mouse), now);  // a setting waiting for its save delay
  const unsigned commits = EEPROM.commits;
  const int activity = g_activity_calls;
  for (int i = 0; i < 3100 / int(cfg::JOY_PERIOD_MS); i++) {
    centre.task(now += cfg::JOY_PERIOD_MS);
    hw::settings_task(now);
  }
  assert(EEPROM.commits == commits + 1);  // the stick's own save check does not keep pushing the delay
  assert(g_activity_calls == activity);   // a stick at rest lets the displays sleep

  mock_adc[cfg::JOY_X_PIN] = 3500;
  for (int i = 0; i < 3; i++) centre.task(now += cfg::JOY_PERIOD_MS);
  assert(g_activity_calls > activity);  // using the stick keeps them awake

  drain();
  mock_reports.clear();
  mock_adc[cfg::JOY_X_PIN] = 3700;  // full right
  centre.task(now += 1000);         // the loop stalled for a second (a flash write)
  drain();
  int dx = 0;
  for (const auto& r : mock_reports) dx += r.dx;
  assert(dx > 100 && dx <= 110);  // 0.1 s of travel at most: no pointer jump

  mock_adc[cfg::JOY_X_PIN] = 2048;
  hw::settings().centre_modes[base] = uint8_t(JoyMode::Inherit);  // the layer's own mode back in flash
  hw::settings_touch(now);
  hw::settings_flush();
}
#endif

static void test_display_pushes_only_the_rows_that_changed() {
  constexpr int w = 4, h = 6;
  uint16_t frame[w * h] = {};
  uint32_t shown[h] = {};
  ui::changed_rows(frame, w, h, shown);  // remember the frame as shown
  ui::RowSpan rows = ui::changed_rows(frame, w, h, shown);
  assert(rows.count == 0);  // an identical frame costs no SPI time
  frame[2 * w + 3] = 0x1234;  // last pixel of row 2
  rows = ui::changed_rows(frame, w, h, shown);
  assert(rows.first == 2 && rows.count == 1);
  frame[1 * w] = 1;  // rows 1 and 4 change, 2 and 3 do not: one band covers them
  frame[4 * w + 1] = 2;
  rows = ui::changed_rows(frame, w, h, shown);
  assert(rows.first == 1 && rows.count == 4);
  frame[5 * w] = frame[5 * w + 1] = 7;  // 7,7,0,0 -> 0,7,7,0: same pixels, other columns
  ui::changed_rows(frame, w, h, shown);
  frame[5 * w] = 0;
  frame[5 * w + 2] = 7;
  rows = ui::changed_rows(frame, w, h, shown);
  assert(rows.first == 5 && rows.count == 1);
  assert(ui::changed_rows(frame, w, h, shown).count == 0);  // the hashes follow every call
}

static void test_host_dashboard_text_formats() {
  char buf[16], temp[8];  // 8 = the GPU line's buffer in render.cpp
  const auto rate = [&](uint32_t bps) { return ui::fmt_rate(buf, sizeof buf, bps), std::string(buf); };
  const auto temperature = [&](int16_t c10) { return ui::fmt_temp(temp, sizeof temp, c10), std::string(temp); };
  const auto percent = [&](uint8_t pct) { return ui::fmt_pct(buf, sizeof buf, pct), std::string(buf); };
  const auto uptime = [&](uint32_t s) { return ui::fmt_uptime(buf, sizeof buf, s), std::string(buf); };
  assert(rate(999) == "999B" && rate(1000) == "1.0K" && rate(999999) == "999.9K");
  assert(rate(1000000) == "1.0M" && rate(UINT32_MAX) == "4.2G");
  assert(temperature(-4) == "0C" && temperature(32767) == "3277C" && temperature(-32767) == "-3277C");
  assert(temperature(mp::HOST_NO_TEMP) == "--C");
  assert(percent(100) == "100%" && percent(101) == "--");
  assert(uptime(59) == "0m" && uptime(86399) == "23h 59m" && uptime(86400) == "1d 0h");

  char part[13];
  const std::string unbroken(60, 'x');  // a full text slot with no space
  assert(ui::wrap12(unbroken.c_str(), part) == 12 && part[12] == 0 && std::string(part) == unbroken.substr(0, 12));
  assert(ui::wrap12("hello world foo", part) == 11 && std::string(part) == "hello world");
  assert(ui::wrap12("abcdefghijkl mno", part) == 12 && std::string(part) == "abcdefghijkl");  // space at column 13
  assert(ui::wrap12("abcdefghijklm nop", part) == 12 && std::string(part) == "abcdefghijkl");  // column 14 is too late
  assert(ui::wrap12("short", part) == 5 && std::string(part) == "short");
  assert(ui::wrap12("", part) == 0 && part[0] == 0);

  char host[ui::HEADER_HOST_CHARS + 1];
  ui::fmt_header_host(host, "ip-172-31-22-143.ec2.internal");  // 29 characters would run into the clock
  assert(std::string(host) == "ip-172-31-22-143.ec2.i");
  ui::fmt_header_host(host, "workstation");
  assert(std::string(host) == "workstation");
}

static void test_console_takes_one_line_per_line_end() {
  app::ConsoleLine console;
  std::vector<std::string> lines;
  const auto type = [&](const char* text) {
    for (; *text; ++text)
      if (console.feed(*text)) lines.push_back(console.text());
  };
  type("info\r\n");  // CRLF, what `pio device monitor` sends: one command, not a command and an empty line
  assert(lines == std::vector<std::string>{"info"});
  type("joy\rcal\n\n");  // CR alone and LF alone still end a line; a lone Enter is an empty line (help)
  assert((lines == std::vector<std::string>{"info", "joy", "cal", ""}));
  lines.clear();
  type("\r\r\n\n");
  assert((lines == std::vector<std::string>{"", "", ""}));
  lines.clear();
  type((std::string(40, 'x') + "\n").c_str());  // longer than the buffer: cut, not overrun
  assert(lines == std::vector<std::string>{std::string(31, 'x')});
}

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

// The wiring as the matrix sees it: a held key pulls its column low while its row is driven low.
static bool g_matrix_down[mp::ROWS][mp::COLS];
static int g_matrix_reads_until_release = -1;  // >= 0: every key is let go after this many column reads
static int matrix_wiring(int pin) {
  if (g_matrix_reads_until_release == 0) memset(g_matrix_down, 0, sizeof g_matrix_down);
  if (g_matrix_reads_until_release >= 0) --g_matrix_reads_until_release;
  for (uint8_t c = 0; c < mp::COLS; ++c) {
    if (cfg::COL_PINS[c] != pin) continue;
    for (uint8_t r = 0; r < mp::ROWS; ++r)
      if (g_matrix_down[r][c] && mock_pins[cfg::ROW_PINS[r]] == LOW) return LOW;
    return HIGH;
  }
  return mock_pins[pin];
}

static void test_matrix_scans_col2row_and_reads_the_boot_combo() {
  memset(g_matrix_down, 0, sizeof g_matrix_down);
  g_matrix_reads_until_release = -1;
  mock_read = matrix_wiring;
  hw::matrix_init();
  for (uint8_t p : cfg::ROW_PINS) assert(mock_pin_mode[p] == OUTPUT && mock_pins[p] == HIGH);
  for (uint8_t p : cfg::COL_PINS) assert(mock_pin_mode[p] == INPUT_PULLUP);

  uint8_t raw[mp::ROWS];
  g_matrix_down[1][2] = true;
  hw::matrix_scan(raw);
  assert(raw[0] == 0 && raw[1] == 0b100 && raw[2] == 0 && raw[3] == 0);
  g_matrix_down[3][2] = true;  // same column, another row: only the driven row reads it
  hw::matrix_scan(raw);
  assert(raw[0] == 0 && raw[1] == 0b100 && raw[2] == 0 && raw[3] == 0b100);
  for (uint8_t p : cfg::ROW_PINS) assert(mock_pins[p] == HIGH);  // no row is left driven

  memset(g_matrix_down, 0, sizeof g_matrix_down);
  assert(!hw::matrix_boot_combo_held());  // a plain plug-in must not reboot into the bootloader
  g_matrix_down[mp::BOOT_COMBO[0][0]][mp::BOOT_COMBO[0][1]] = true;
  assert(!hw::matrix_boot_combo_held());  // one key of the two
  g_matrix_down[mp::BOOT_COMBO[1][0]][mp::BOOT_COMBO[1][1]] = true;
  assert(hw::matrix_boot_combo_held());
  g_matrix_reads_until_release = mp::ROWS * mp::COLS;  // held for the first scan only
  assert(!hw::matrix_boot_combo_held());
  g_matrix_reads_until_release = -1;
  mock_read = nullptr;
}

int main() {
  test_usb_drops_keys_pressed_before_the_first_mount();
  test_usb_preserves_press_and_release();
  test_usb_retries_failed_reports();
  test_usb_resends_after_remount();
  test_usb_keeps_a_repeated_tap_while_busy();
  test_usb_mount_publishes_a_held_stick_click();
  test_usb_overflow_eventually_releases();
  test_motion_does_not_pause_generated_keys();
  test_motion_keeps_the_held_button();
  test_suspended_click_requests_wake();
  test_input_is_dropped_while_the_host_refuses_wake();
  test_usb_finishes_the_resync_after_a_refused_wake();
  test_wheel_step_is_sent_once_and_dropped_while_suspended();
  test_usb_reports_the_lock_leds();
  test_button_report_in_flight_is_not_delivered();
  test_motion_waits_for_a_busy_endpoint_but_not_without_limit();
  test_settings_isolate_editions();
  test_diagnostics_do_not_block_input();
  test_settings_save_waits_after_a_late_stamp();
  test_settings_flush_saves_before_a_reboot();
  test_display_pushes_only_the_rows_that_changed();
  test_host_dashboard_text_formats();
  test_matrix_scans_col2row_and_reads_the_boot_combo();
  test_console_takes_one_line_per_line_end();
#ifdef TEST_JOYSTICK
  test_stick_mode_cycle_and_click();
  test_stick_saves_the_learned_range_not_the_centre();
  test_stick_motion_wakes_and_does_not_starve_the_save();
  test_host_link_round_trip();
  puts("28 hardware/control regression groups passed");
#else
  test_enc_diagnostic_reports_the_pins();
  test_knob_turns_reverses_and_keeps_its_preset();
  test_knob_switch_is_debounced();
  test_keymap_fn_keys_reach_the_knob();
  test_host_link_round_trip();
  puts("29 hardware/control regression groups passed");
#endif
}
