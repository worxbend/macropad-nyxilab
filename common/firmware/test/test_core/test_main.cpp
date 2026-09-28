// Host-side unit tests of lib/nyx_core (run: pio test -e native in common/firmware).
// Uses its own small keymap so editing an edition's keymap.cpp never breaks them.
#include <unity.h>

#include <string>
#include <vector>

#include "core/debounce.h"
#include "core/engine.h"
#include "core/led_fx.h"

using namespace mp;

// ------------------------------------------------------------ test keymap
namespace mp {
const TapHoldDef TAP_HOLDS[] = {{LNEXT, MO(2), 200}};
const char* const MACROS[] = {"Hi!"};
const EncoderDef PRESETS[] = {
    {CC(cc::VOL_UP), CC(cc::VOL_DOWN), CC(cc::MUTE), "VOLUME"},  // same as BASE's own
    {WHEEL(-1), WHEEL(1), MB(MB_MIDDLE), "SCROLL"},
    {KC(MOD_LCTRL, hid::EQUAL), KC(MOD_LCTRL, hid::MINUS), KC(MOD_LCTRL, hid::N0), "ZOOM"},
};
const Layer LAYERS[] = {
    {"BASE", 0, KNOB(CC(cc::VOL_UP), CC(cc::VOL_DOWN), CC(cc::MUTE), "VOL"), {
        {{K(hid::A), "a"}, {CC(cc::MUTE), "mute"}, {MB(MB_LEFT), "lmb"}},
        {{KC(MOD_LCTRL, hid::C), "copy"}, {MACRO(0), "hi"}, {TG(1), "tg1"}},
        {{SYS(SYS_BRIGHT_UP), "bri"}, {JOY(JoyMode::Scroll), "joy"}, {KNOB_CYCLE, "knob"}},
        {{TH(0), "L"}, {K(hid::B), "b"}, {K(hid::C), "c"}},
    }},
    {"SECOND", 0, CENTRE_INHERIT, {
        {{K(hid::X), "x"}, {___, ""}, {___, ""}},
        {{___, ""}, {___, ""}, {___, ""}},
        {{___, ""}, {___, ""}, {___, ""}},
        {{TH(0), "L"}, {___, ""}, {___, ""}},
    }},
    {"FN", 0, KNOB(K(hid::RIGHT), K(hid::LEFT), K(hid::ENTER), "ARROWS"), {
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

// The host does what the rotary edition's KnobCentre does with a cycle request.
struct Host : EngineHost {
  std::vector<int> sys;
  std::vector<int> centre;
  Engine* eng = nullptr;
  void on_sys(SysCmd c) override { sys.push_back(c); }
  void on_centre(uint8_t m) override {
    centre.push_back(m);
    if (m == ENC_CYCLE && eng) eng->cycle_enc_preset(eng->base_layer());
  }
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

void test_sys_and_centre_callbacks() {
  Host h;
  Engine e(h);
  e.process(press(2, 0, 0));
  e.process(press(2, 1, 5));
  TEST_ASSERT_EQUAL(1, h.sys.size());
  TEST_ASSERT_EQUAL(SYS_BRIGHT_UP, h.sys[0]);
  TEST_ASSERT_EQUAL(1, h.centre.size());
  TEST_ASSERT_EQUAL(int(JoyMode::Scroll), h.centre[0]);
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

// ------------------------------------------------------------------- knob
void test_knob_taps_consumer_keys_one_after_another() {
  Host h;
  Engine e(h);
  e.encoder_step(+1, 0);
  e.encoder_step(+1, 0);  // two detents at once: queued
  TEST_ASSERT_EQUAL_HEX16(cc::VOL_UP, e.hid().consumer);
  int presses = 0;
  uint16_t prev = e.hid().consumer;
  for (uint32_t t = 1; t < 60; t++) {
    e.tick(t);
    if (e.hid().consumer && !prev) presses++;
    prev = e.hid().consumer;
  }
  TEST_ASSERT_EQUAL(1, presses);  // the second tap, after a release and a gap
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);
  e.encoder_step(-1, 100);
  TEST_ASSERT_EQUAL_HEX16(cc::VOL_DOWN, e.hid().consumer);
  TEST_ASSERT_EQUAL(1, e.encoder_position());
}

void test_knob_press_is_held_and_latched() {
  Host h;
  Engine e(h);
  e.encoder_button(true, 0);
  TEST_ASSERT_EQUAL_HEX16(cc::MUTE, e.hid().consumer);
  e.process(press(3, 0, 10));
  e.tick(300);  // FN now on top, knob still held
  e.encoder_button(false, 310);
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);  // released what was pressed
  TEST_ASSERT_FALSE(e.hid().has_key(hid::ENTER));
}

void test_knob_fn_layer_binding_and_hold_on_turn() {
  Host h;
  Engine e(h);
  e.process(press(3, 0, 0));  // LAYER key down, tap-hold undecided
  e.encoder_step(+1, 20);     // turning while held resolves it as FN
  TEST_ASSERT_EQUAL(2, e.top_layer());
  TEST_ASSERT_TRUE(e.hid().has_key(hid::RIGHT));
  TEST_ASSERT_EQUAL_STRING("ARROWS", e.encoder_binding().label);
  e.tick(40);
  TEST_ASSERT_FALSE(e.hid().has_key(hid::RIGHT));
  e.process(release(3, 0, 50));
  TEST_ASSERT_EQUAL(0, e.base_layer());  // no layer change from that tap
  TEST_ASSERT_EQUAL_STRING("VOL", e.encoder_binding().label);
}

void test_knob_transparent_layer_falls_through() {
  Host h;
  Engine e(h);
  e.process(press(1, 2, 0));  // TG(1): SECOND on top of BASE, its knob binding is transparent
  e.process(release(1, 2, 5));
  TEST_ASSERT_EQUAL(1, e.top_layer());
  TEST_ASSERT_EQUAL_STRING("VOL", e.encoder_binding().label);
}

void test_knob_cycle_skips_duplicates_and_notifies() {
  Host h;
  Engine e(h);
  h.eng = &e;
  e.set_encoder_presets(PRESETS, 3);
  const char* seen[5];
  uint8_t presets[5];
  for (int i = 0; i < 4; i++) {
    e.process(press(2, 2, uint32_t(i * 20)));
    e.process(release(2, 2, uint32_t(i * 20 + 5)));
    seen[i] = e.encoder_binding().label;
    presets[i] = e.enc_preset(0);
  }
  TEST_ASSERT_EQUAL_STRING("SCROLL", seen[0]);  // VOLUME preset = BASE's own, skipped
  TEST_ASSERT_EQUAL_STRING("ZOOM", seen[1]);
  TEST_ASSERT_EQUAL_STRING("VOL", seen[2]);  // back to the layer's own
  TEST_ASSERT_EQUAL_STRING("SCROLL", seen[3]);
  TEST_ASSERT_EQUAL(4, h.centre.size());
  TEST_ASSERT_EQUAL(ENC_CYCLE, h.centre[0]);
  TEST_ASSERT_EQUAL(1, presets[0]);
  TEST_ASSERT_EQUAL(ENC_LAYER_DEFAULT, presets[2]);
}

void test_knob_wheel_steps_and_preset_restore() {
  Host h;
  Engine e(h);
  e.set_encoder_presets(PRESETS, 3);
  e.set_enc_preset(0, 1);  // SCROLL, as restored from flash
  e.encoder_step(+1, 0);
  e.encoder_step(+1, 1);
  e.encoder_step(-1, 2);
  int8_t w = 0, pan = 0;
  e.take_wheel(w, pan);
  TEST_ASSERT_EQUAL(-1, w);  // clockwise scrolls down
  TEST_ASSERT_EQUAL(0, pan);
  e.take_wheel(w, pan);
  TEST_ASSERT_EQUAL(0, w);
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mouse_buttons);
  e.set_enc_preset(0, 200);  // garbage in flash -> layer default
  TEST_ASSERT_EQUAL(ENC_LAYER_DEFAULT, e.enc_preset(0));
}

void test_knob_queue_overflow_drops_steps() {
  Host h;
  Engine e(h);
  for (int i = 0; i < 40; i++) e.encoder_step(+1, 0);
  int presses = 1;  // the first one started immediately
  uint16_t prev = e.hid().consumer;
  for (uint32_t t = 1; t < 1000; t++) {
    e.tick(t);
    if (e.hid().consumer && !prev) presses++;
    prev = e.hid().consumer;
  }
  TEST_ASSERT_EQUAL(1 + Engine::TAP_QUEUE, presses);
  TEST_ASSERT_EQUAL(40, e.encoder_position());  // the dial still shows every detent
}

// -------------------------------------------------------------------- LEDs
void test_led_layer_colour_and_flash() {
  LedInput in;
  in.color565 = 0xF800;  // red
  Rgb px[16];
  led_frame(in, 0, px, 16);
  const uint8_t rest = px[0].r;
  TEST_ASSERT_TRUE(rest > 100);
  TEST_ASSERT_EQUAL(0, px[0].g);
  TEST_ASSERT_EQUAL(0, px[15].b);
  in.since_hit_ms = 0;
  led_frame(in, 0, px, 16);
  TEST_ASSERT_TRUE(px[0].r > rest);  // flash on a key press
  in.since_hit_ms = LED_PULSE_MS;
  led_frame(in, 0, px, 16);
  TEST_ASSERT_EQUAL(rest, px[0].r);  // and back
}

void test_led_levels_budget_and_off() {
  LedInput in;
  in.color565 = 0xFFFF;  // white: the worst case for current
  Rgb px[16];
  in.budget_ma = 250;
  led_frame(in, 0, px, 16);
  TEST_ASSERT_TRUE(led_current_ma(px, 16) <= 250);
  TEST_ASSERT_TRUE(led_current_ma(px, 16) > 200);
  in.level = 0;  // host asleep / backlight off
  led_frame(in, 0, px, 16);
  TEST_ASSERT_EQUAL(0, led_current_ma(px, 16));
  in.level = 255;
  in.mode = LedMode::Off;
  led_frame(in, 0, px, 16);
  TEST_ASSERT_EQUAL(0, led_current_ma(px, 16));
}

void test_led_rainbow_and_breathe() {
  LedInput in;
  in.mode = LedMode::Rainbow;
  Rgb a[16], b[16];
  led_frame(in, 0, a, 16);
  TEST_ASSERT_FALSE(a[0].r == a[8].r && a[0].g == a[8].g && a[0].b == a[8].b);  // opposite sides differ
  led_frame(in, LED_RAINBOW_MS / 2, b, 16);
  TEST_ASSERT_EQUAL(a[8].r, b[0].r);  // the wheel turns around the chain
  TEST_ASSERT_EQUAL(a[8].g, b[0].g);
  in.mode = LedMode::Breathe;
  in.color565 = 0x001F;
  led_frame(in, 0, a, 1);
  led_frame(in, LED_BREATHE_MS / 2, b, 1);
  TEST_ASSERT_TRUE(b[0].b > 4 * a[0].b);
  TEST_ASSERT_EQUAL_STRING("RAINBOW", led_mode_name(LedMode::Rainbow));
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
  RUN_TEST(test_sys_and_centre_callbacks);
  RUN_TEST(test_macro_types_text);
  RUN_TEST(test_ascii_mapping);
  RUN_TEST(test_knob_taps_consumer_keys_one_after_another);
  RUN_TEST(test_knob_press_is_held_and_latched);
  RUN_TEST(test_knob_fn_layer_binding_and_hold_on_turn);
  RUN_TEST(test_knob_transparent_layer_falls_through);
  RUN_TEST(test_knob_cycle_skips_duplicates_and_notifies);
  RUN_TEST(test_knob_wheel_steps_and_preset_restore);
  RUN_TEST(test_knob_queue_overflow_drops_steps);
  RUN_TEST(test_led_layer_colour_and_flash);
  RUN_TEST(test_led_levels_budget_and_off);
  RUN_TEST(test_led_rainbow_and_breathe);
  return UNITY_END();
}
