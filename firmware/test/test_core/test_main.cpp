// Host-side unit tests for src/core (run: pio test -e native).
// Uses its own small keymap so editing src/core/keymap.cpp never breaks them.
#include <unity.h>

#include <string>
#include <vector>

#include "core/debounce.h"
#include "core/engine.h"
#include "core/joystick.h"

using namespace mp;

// ------------------------------------------------------------ test keymap
namespace mp {
const TapHoldDef TAP_HOLDS[] = {{LNEXT, MO(2), 200}};
const char* const MACROS[] = {"Hi!"};
const Layer LAYERS[] = {
    {"BASE", 0, JoyMode::Mouse, {
        {{K(hid::A), "a"}, {CC(cc::MUTE), "mute"}, {MB(MB_LEFT), "lmb"}},
        {{KC(MOD_LCTRL, hid::C), "copy"}, {MACRO(0), "hi"}, {TG(1), "tg1"}},
        {{SYS(SYS_BRIGHT_UP), "bri"}, {JOY(JoyMode::Cycle), "joy"}, {XX, ""}},
        {{TH(0), "L"}, {K(hid::B), "b"}, {K(hid::C), "c"}},
    }},
    {"SECOND", 0, JoyMode::Scroll, {
        {{K(hid::X), "x"}, {___, ""}, {___, ""}},
        {{___, ""}, {___, ""}, {___, ""}},
        {{___, ""}, {___, ""}, {___, ""}},
        {{TH(0), "L"}, {___, ""}, {___, ""}},
    }},
    {"FN", 0, JoyMode::Inherit, {
        {{K(hid::F1), "f1"}, {TO(1), "to1"}, {___, ""}},
        {{___, ""}, {___, ""}, {___, ""}},
        {{___, ""}, {___, ""}, {___, ""}},
        {{___, ""}, {K(hid::F2), "f2"}, {___, ""}},
    }},
};
const uint8_t NUM_LAYERS = 3;
const uint8_t FN_LAYER = 2;
const uint8_t NUM_TAP_HOLDS = 1;
const uint8_t NUM_MACROS = 1;
}  // namespace mp

struct Host : EngineHost {
  std::vector<int> sys;
  std::vector<int> joy;
  void on_sys(SysCmd c) override { sys.push_back(c); }
  void on_joy_mode(JoyMode m) override { joy.push_back(int(m)); }
};

static KeyEvent press(uint8_t r, uint8_t c, uint32_t t) { return {r, c, true, t}; }
static KeyEvent release(uint8_t r, uint8_t c, uint32_t t) { return {r, c, false, t}; }

void setUp() {}
void tearDown() {}

// ------------------------------------------------------------------ debounce
void test_debounce_filters_bounce() {
  Debouncer d(5);
  std::vector<KeyEvent> ev;
  auto sink = [&](const KeyEvent& e) { ev.push_back(e); };
  uint8_t raw[ROWS] = {};
  const uint8_t pattern[] = {1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1};
  uint32_t t = 0;
  for (uint8_t v : pattern) {
    raw[2] = v << 1;  // row 2, col 1
    d.update(raw, t++, sink);
  }
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_TRUE(ev[0].pressed);
  TEST_ASSERT_EQUAL(2, ev[0].row);
  TEST_ASSERT_EQUAL(1, ev[0].col);
  TEST_ASSERT_TRUE(d.is_down(2, 1));
  raw[2] = 0;
  for (int i = 0; i < 4; i++) d.update(raw, t++, sink);
  TEST_ASSERT_EQUAL(1, ev.size());  // not yet
  for (int i = 0; i < 3; i++) d.update(raw, t++, sink);
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_FALSE(ev[1].pressed);
}

// ------------------------------------------------------------------- engine
void test_plain_key_and_modifier() {
  Host h;
  Engine e(h);
  e.process(press(0, 0, 0));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::A));
  e.process(press(1, 0, 1));  // Ctrl+C
  TEST_ASSERT_TRUE(e.hid().has_key(hid::C));
  TEST_ASSERT_EQUAL_HEX8(MOD_LCTRL, e.hid().mods);
  e.process(release(1, 0, 2));
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mods);
  TEST_ASSERT_FALSE(e.hid().has_key(hid::C));
  e.process(release(0, 0, 3));
  TEST_ASSERT_FALSE(e.hid().has_key(hid::A));
}

void test_consumer_and_mouse_button() {
  Host h;
  Engine e(h);
  e.process(press(0, 1, 0));
  TEST_ASSERT_EQUAL_HEX16(cc::MUTE, e.hid().consumer);
  e.process(press(0, 2, 1));
  TEST_ASSERT_EQUAL_HEX8(MB_LEFT, e.hid().mouse_buttons);
  e.process(release(0, 1, 2));
  e.process(release(0, 2, 3));
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mouse_buttons);
}

void test_tap_goes_to_next_layer_skipping_fn() {
  Host h;
  Engine e(h);
  e.process(press(3, 0, 0));
  e.process(release(3, 0, 50));  // tap
  TEST_ASSERT_EQUAL(1, e.base_layer());
  e.tick(100);
  e.process(press(3, 0, 200));
  e.process(release(3, 0, 250));
  TEST_ASSERT_EQUAL(0, e.base_layer());  // wrapped, FN skipped
}

void test_hold_timeout_activates_fn() {
  Host h;
  Engine e(h);
  e.process(press(3, 0, 0));
  e.tick(100);
  TEST_ASSERT_EQUAL(0, e.top_layer());
  e.tick(201);
  TEST_ASSERT_EQUAL(2, e.top_layer());
  e.process(press(0, 0, 250));  // F1 on FN
  TEST_ASSERT_TRUE(e.hid().has_key(hid::F1));
  e.process(release(0, 0, 260));
  e.process(release(3, 0, 300));
  TEST_ASSERT_EQUAL(0, e.top_layer());
  TEST_ASSERT_EQUAL(0, e.base_layer());  // a hold never changes the base layer
}

void test_hold_on_other_key_press() {
  Host h;
  Engine e(h);
  e.process(press(3, 0, 0));
  e.process(press(3, 1, 20));  // within the term: FN resolves immediately
  TEST_ASSERT_TRUE(e.hid().has_key(hid::F2));
  TEST_ASSERT_FALSE(e.hid().has_key(hid::B));
  e.process(release(3, 1, 30));
  e.process(release(3, 0, 40));
  TEST_ASSERT_EQUAL(0, e.top_layer());
}

void test_transparent_falls_through() {
  Host h;
  Engine e(h);
  e.process(press(3, 0, 0));
  e.tick(300);                  // FN held
  e.process(press(3, 2, 310));  // FN is transparent here -> base 'c'
  TEST_ASSERT_TRUE(e.hid().has_key(hid::C));
}

void test_to_layer_from_fn() {
  Host h;
  Engine e(h);
  e.process(press(3, 0, 0));
  e.tick(300);
  e.process(press(0, 1, 310));  // TO(1)
  e.process(release(0, 1, 320));
  TEST_ASSERT_EQUAL(1, e.base_layer());
  TEST_ASSERT_EQUAL(2, e.top_layer());  // FN still held
  e.process(release(3, 0, 400));
  TEST_ASSERT_EQUAL(1, e.top_layer());
  e.process(press(0, 0, 500));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::X));
}

void test_toggle_layer() {
  Host h;
  Engine e(h);
  e.process(press(1, 2, 0));
  e.process(release(1, 2, 10));
  TEST_ASSERT_EQUAL(1, e.top_layer());
  e.process(press(0, 0, 20));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::X));
}

void test_key_release_uses_press_layer() {
  Host h;
  Engine e(h);
  e.process(press(3, 0, 0));
  e.tick(300);                  // FN on
  e.process(press(0, 0, 310));  // F1
  e.process(release(3, 0, 320));  // FN off while F1 still held
  e.process(release(0, 0, 330));
  TEST_ASSERT_FALSE(e.hid().has_key(hid::F1));
  TEST_ASSERT_FALSE(e.hid().has_key(hid::A));
}

void test_sys_and_joy_callbacks() {
  Host h;
  Engine e(h);
  e.process(press(2, 0, 0));
  e.process(press(2, 1, 5));
  TEST_ASSERT_EQUAL(1, h.sys.size());
  TEST_ASSERT_EQUAL(SYS_BRIGHT_UP, h.sys[0]);
  TEST_ASSERT_EQUAL(1, h.joy.size());
  TEST_ASSERT_EQUAL(int(JoyMode::Cycle), h.joy[0]);
}

void test_macro_types_text() {
  Host h;
  Engine e(h);
  std::string typed;
  HidState last{};
  e.process(press(1, 1, 0));
  e.process(release(1, 1, 5));
  for (uint32_t t = 0; t < 200; t++) {
    e.tick(t);
    const HidState& s = e.hid();
    if (s != last && s.keys[0]) {
      uint8_t u = s.keys[0];
      bool shift = s.mods & MOD_LSHIFT;
      char c = '?';
      if (u >= hid::A && u <= hid::Z) c = char((shift ? 'A' : 'a') + (u - hid::A));
      if (u == hid::N1 && shift) c = '!';
      typed += c;
    }
    last = s;
  }
  TEST_ASSERT_EQUAL_STRING("Hi!", typed.c_str());
  TEST_ASSERT_FALSE(e.macro_running());
}

void test_ascii_mapping() {
  uint8_t m, u;
  ascii_to_hid('?', m, u);
  TEST_ASSERT_EQUAL_HEX8(hid::SLSH, u);
  TEST_ASSERT_EQUAL_HEX8(MOD_LSHIFT, m);
  ascii_to_hid('0', m, u);
  TEST_ASSERT_EQUAL_HEX8(hid::N0, u);
  TEST_ASSERT_EQUAL_HEX8(0, m);
  ascii_to_hid('\x01', m, u);
  TEST_ASSERT_EQUAL_HEX8(0, u);
}

// ----------------------------------------------------------------- joystick
static Joystick make_joy() {
  Joystick j;
  JoyCal c;
  c.cx = 2000;
  c.cy = 2100;
  c.min_x = c.min_y = 100;
  c.max_x = c.max_y = 4000;
  j.set_cal(c);
  return j;
}

void test_joystick_center_is_still() {
  Joystick j = make_joy();
  JoyOutput o;
  for (int i = 0; i < 50; i++) j.update(2010, 2090, 0.01f, JoyMode::Mouse, o);
  TEST_ASSERT_EQUAL(0, o.dx);
  TEST_ASSERT_EQUAL(0, o.dy);
  TEST_ASSERT_EQUAL_FLOAT(0.f, j.x());
}

void test_joystick_full_right_moves_at_max_speed() {
  Joystick j = make_joy();
  JoyOutput o;
  int total = 0;
  for (int i = 0; i < 100; i++) {  // 1 s
    j.update(4000, 2100, 0.01f, JoyMode::Mouse, o);
    total += o.dx;
    TEST_ASSERT_EQUAL(0, o.dy);
  }
  TEST_ASSERT_INT_WITHIN(20, int(j.config().mouse_speed), total);
}

void test_joystick_curve_is_monotonic() {
  Joystick j = make_joy();
  JoyOutput o;
  float prev = 0;
  for (int raw = 2000; raw <= 4000; raw += 100) {
    j.update(uint16_t(raw), 2100, 0.01f, JoyMode::Off, o);
    TEST_ASSERT_TRUE(j.x() >= prev);
    prev = j.x();
  }
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.f, prev);
}

void test_joystick_up_scrolls_up_and_pointer_up() {
  Joystick j = make_joy();
  JoyOutput o;
  int wheel = 0;
  for (int i = 0; i < 100; i++) {
    j.update(2000, 4000, 0.01f, JoyMode::Scroll, o);
    wheel += o.wheel;
  }
  TEST_ASSERT_TRUE(wheel > 10);
  j.update(2000, 4000, 0.05f, JoyMode::Mouse, o);
  TEST_ASSERT_TRUE(o.dy < 0);
}

void test_joystick_arrows_hysteresis() {
  Joystick j = make_joy();
  JoyOutput o;
  j.update(2000 + uint16_t(0.6f * 2000), 2100, 0.01f, JoyMode::Arrows, o);
  TEST_ASSERT_EQUAL(1, o.n_keys);
  TEST_ASSERT_EQUAL_HEX8(hid::RIGHT, o.keys[0]);
  j.update(2000 + uint16_t(0.45f * 2000), 2100, 0.01f, JoyMode::Arrows, o);  // between thresholds
  TEST_ASSERT_EQUAL(1, o.n_keys);
  j.update(2000 + uint16_t(0.2f * 2000), 2100, 0.01f, JoyMode::Arrows, o);
  TEST_ASSERT_EQUAL(0, o.n_keys);
}

void test_joystick_invert_and_swap() {
  Joystick j = make_joy();
  JoyConfig c;
  c.invert_x = true;
  c.swap_xy = true;
  j.set_config(c);
  JoyOutput o;
  j.update(2000, 4000, 0.01f, JoyMode::Off, o);  // raw "up" becomes x, inverted
  TEST_ASSERT_TRUE(j.x() < -0.9f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, j.y());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_debounce_filters_bounce);
  RUN_TEST(test_plain_key_and_modifier);
  RUN_TEST(test_consumer_and_mouse_button);
  RUN_TEST(test_tap_goes_to_next_layer_skipping_fn);
  RUN_TEST(test_hold_timeout_activates_fn);
  RUN_TEST(test_hold_on_other_key_press);
  RUN_TEST(test_transparent_falls_through);
  RUN_TEST(test_to_layer_from_fn);
  RUN_TEST(test_toggle_layer);
  RUN_TEST(test_key_release_uses_press_layer);
  RUN_TEST(test_sys_and_joy_callbacks);
  RUN_TEST(test_macro_types_text);
  RUN_TEST(test_ascii_mapping);
  RUN_TEST(test_joystick_center_is_still);
  RUN_TEST(test_joystick_full_right_moves_at_max_speed);
  RUN_TEST(test_joystick_curve_is_monotonic);
  RUN_TEST(test_joystick_up_scrolls_up_and_pointer_up);
  RUN_TEST(test_joystick_arrows_hysteresis);
  RUN_TEST(test_joystick_invert_and_swap);
  return UNITY_END();
}
