// Host-side unit tests of lib/nyx_core (run: pio test -e native in common/firmware).
// Uses its own small keymap so editing an edition's keymap.cpp never breaks them.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "core/debounce.h"
#include "core/engine.h"
#include "core/host_proto.h"
#include "core/led_fx.h"

using namespace mp;

// ------------------------------------------------------------ test keymap
namespace mp {
const TapHoldDef TAP_HOLDS[] = {{LNEXT, MO(2), 200}, {K(hid::ESC), MO(2), 200}};
const char* const MACROS[] = {"Hi!", "a\x01" "b"};  // \x01 has no key
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
        {{K(hid::X), "x"}, {___, ""}, {MO(1), "hold1"}},
        {{TH(1), "esc"}, {___, ""}, {___, ""}},
        {{HOST(7), "h7"}, {___, ""}, {___, ""}},
        {{TH(0), "L"}, {___, ""}, {KC(MOD_LSHIFT, 0), "shift"}},
    }},
    {"FN", 0, KNOB(K(hid::RIGHT), K(hid::LEFT), K(hid::ENTER), "ARROWS"), {
        {{K(hid::F1), "f1"}, {TO(1), "to1"}, {___, ""}},
        {{___, ""}, {K(hid::F3), "f3"}, {K(hid::F4), "f4"}},
        {{K(hid::F5), "f5"}, {K(hid::F6), "f6"}, {___, ""}},
        {{___, ""}, {K(hid::F2), "f2"}, {___, ""}},
    }},
};
const uint8_t NUM_LAYERS = 3;
const uint8_t FN_LAYER = 2;
const uint8_t NUM_TAP_HOLDS = 2;
const uint8_t NUM_MACROS = 2;
}  // namespace mp

// The host does what the rotary edition's KnobCentre does with a cycle request.
struct Host : EngineHost {
  std::vector<int> sys;
  std::vector<int> centre;
  std::vector<int> host;  // id * 2 + pressed
  Engine* eng = nullptr;
  void on_sys(SysCmd c) override { sys.push_back(c); }
  void on_centre(uint8_t m) override {
    centre.push_back(m);
    if (m == ENC_CYCLE && eng) eng->cycle_enc_preset(eng->base_layer());
  }
  void on_host(uint8_t id, bool pressed) override { host.push_back(id * 2 + (pressed ? 1 : 0)); }
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

void test_debounce_restarts_timer_after_a_spike() {
  Debouncer d(5);
  int n = 0;
  auto sink = [&](const KeyEvent&) { n++; };
  uint8_t raw[ROWS] = {};
  raw[2] = 1; d.update(raw, 0, sink);
  raw[2] = 0; d.update(raw, 1, sink);
  raw[2] = 1; d.update(raw, 6, sink);  // second spike: must not count from t = 0
  raw[2] = 0; d.update(raw, 7, sink);
  TEST_ASSERT_EQUAL(0, n);
  raw[2] = 1; d.update(raw, 10, sink);  // steady press from t = 10
  d.update(raw, 14, sink);
  TEST_ASSERT_EQUAL(0, n);  // 4 ms: not yet
  d.update(raw, 15, sink);
  TEST_ASSERT_EQUAL(1, n);  // exactly at 5 ms
}

void test_debounce_release_keeps_other_keys_of_the_row() {
  Debouncer d(5);
  std::vector<KeyEvent> ev;
  auto sink = [&](const KeyEvent& e) { ev.push_back(e); };
  uint8_t raw[ROWS] = {};
  raw[2] = 0b11; d.update(raw, 0, sink); d.update(raw, 5, sink);
  TEST_ASSERT_EQUAL(2, ev.size());
  raw[2] = 0b01; d.update(raw, 10, sink); d.update(raw, 15, sink);
  TEST_ASSERT_EQUAL(3, ev.size());
  TEST_ASSERT_FALSE(ev[2].pressed);
  TEST_ASSERT_EQUAL(1, ev[2].col);
  TEST_ASSERT_TRUE(d.is_down(2, 0));
  TEST_ASSERT_FALSE(d.is_down(2, 1));
  d.update(raw, 25, sink);
  TEST_ASSERT_EQUAL(3, ev.size());  // no ghost re-press of (2,0)
}

void test_debounce_across_millis_wrap() {
  Debouncer d(5);
  int n = 0;
  auto sink = [&](const KeyEvent&) { n++; };
  const uint32_t t0 = 0xFFFFFFFDu;  // t0 + 5 wraps
  uint8_t raw[ROWS] = {};
  raw[0] = 1;
  d.update(raw, t0, sink);
  d.update(raw, t0 + 1, sink);  // still before the wrap
  d.update(raw, t0 + 4, sink);
  TEST_ASSERT_EQUAL(0, n);
  d.update(raw, t0 + 5, sink);
  TEST_ASSERT_EQUAL(1, n);
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

void test_shortcut_modifiers_stay_off_a_later_key() {
  Host h;
  Engine e(h);
  e.process(press(1, 0, 0));  // Ctrl+C, still down when the next key lands (a fast roll)
  e.process(press(0, 0, 1));  // A
  TEST_ASSERT_TRUE(e.hid().has_key(hid::A));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::C));
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mods);  // plain A, not Ctrl+A
  e.process(release(0, 0, 2));
  TEST_ASSERT_EQUAL_HEX8(MOD_LCTRL, e.hid().mods);  // the shortcut is the newest key again
  e.process(release(1, 0, 3));
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mods);
}

void test_bare_modifier_joins_a_held_shortcut() {
  Host h;
  Engine e(h);
  e.process(press(1, 0, 0));  // Ctrl+C, held
  e.process(press(1, 2, 1));  // TG(1): SECOND has a bare Shift
  e.process(release(1, 2, 2));
  e.process(press(3, 2, 3));  // Shift
  TEST_ASSERT_EQUAL_HEX8(MOD_LCTRL | MOD_LSHIFT, e.hid().mods);  // it adds to the shortcut, it does not replace it
  e.process(press(0, 0, 4));  // X: a later key still gets the bare modifier, not the shortcut's
  TEST_ASSERT_EQUAL_HEX8(MOD_LSHIFT, e.hid().mods);
  e.process(release(0, 0, 5));
  e.process(release(3, 2, 6));
  TEST_ASSERT_EQUAL_HEX8(MOD_LCTRL, e.hid().mods);
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

void test_layer_actions_ignore_undefined_layers() {
  Host h;
  Engine e(h);
  // a keymap typo like TG(5) with 3 layers must not put a non-existent layer on top
  const Action bad[] = {TG(5), MO(7), TG(MAX_LAYERS), MO(MAX_LAYERS + 1)};
  for (Action a : bad) {
    e.queue_tap(a, 0);
    TEST_ASSERT_EQUAL(0, e.top_layer());
    TEST_ASSERT_EQUAL_HEX32(1, e.layer_mask());
  }
}

void test_momentary_layer_preserves_toggle() {
  Host h;
  Engine e(h);
  e.process(press(1, 2, 0));  // TG(1)
  e.process(release(1, 2, 10));
  e.process(press(0, 2, 20));  // MO(1)
  e.process(release(0, 2, 30));
  TEST_ASSERT_EQUAL(1, e.top_layer());  // MO release must not clear TG.
  e.process(press(0, 2, 40));
  e.process(press(1, 2, 50));  // toggle off, while MO(1) is held
  e.process(release(1, 2, 60));
  TEST_ASSERT_EQUAL(1, e.top_layer());  // TG off must not clear MO.
  e.process(release(0, 2, 70));
  TEST_ASSERT_EQUAL(0, e.top_layer());
}

void test_to_clears_toggle_but_keeps_momentary_layer() {
  Host h;
  Engine e(h);
  e.process(press(1, 2, 0));
  e.process(release(1, 2, 10));
  e.process(press(0, 2, 20));
  e.set_base_layer(0);
  TEST_ASSERT_EQUAL(1, e.top_layer());
  e.process(release(0, 2, 30));
  TEST_ASSERT_EQUAL(0, e.top_layer());
}

void test_generated_taps_wait_for_usb_delivery() {
  Host h;
  Engine e(h);
  e.encoder_step(+1, 0);
  e.encoder_step(+1, 0);
  e.tick(100, false);
  TEST_ASSERT_EQUAL_HEX16(cc::VOL_UP, e.hid().consumer);
  e.tick(106, true);
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);
  e.tick(200, false);
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);
  e.tick(206, true);
  TEST_ASSERT_EQUAL_HEX16(cc::VOL_UP, e.hid().consumer);
}

void test_macro_waits_for_usb_delivery() {
  Host h;
  Engine e(h);
  e.process(press(1, 1, 0));
  e.tick(0, false);  // first character must queue even if the host needs waking
  TEST_ASSERT_TRUE(e.hid().has_key(hid::H));
  e.tick(100, false);
  TEST_ASSERT_TRUE(e.hid().has_key(hid::H));
  e.tick(108, true);
  TEST_ASSERT_FALSE(e.hid().has_key(hid::H));
  e.tick(200, false);
  TEST_ASSERT_FALSE(e.hid().has_key(hid::I));
  e.tick(208, true);
  TEST_ASSERT_TRUE(e.hid().has_key(hid::I));
}

void test_repeated_key_taps_while_host_asleep_are_released() {
  Host h;
  Engine e(h);
  e.process(press(1, 2, 0));  // TG(1): tap = Esc, hold = FN
  e.process(release(1, 2, 10));
  for (uint32_t t = 100; t < 300; t += 100) {  // two taps before USB is ready to release the first
    e.process(press(1, 0, t));
    e.tick(t, false);
    e.process(release(1, 0, t + 50));
    e.tick(t + 50, false);
  }
  TEST_ASSERT_TRUE(e.hid().has_key(hid::ESC));
  e.tick(400, true);
  TEST_ASSERT_FALSE(e.hid().has_key(hid::ESC));  // not stuck
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

// ------------------------------------------------- report limits and guards
void test_report_holds_six_keys_and_one_of_each_usage() {
  Host h;
  Engine e(h);
  e.process(press(0, 0, 0));  // A
  e.process(press(3, 1, 1));  // B
  e.process(press(3, 2, 2));  // C
  e.process(press(1, 0, 3));  // Ctrl+C: C again, one slot
  e.tick(3 + Engine::TAP_MS);  // past the gap that shows the host the second C
  const uint8_t extra[8] = {hid::F1, hid::F2, hid::UP, hid::LEFT, hid::RIGHT, hid::ENTER, hid::X, hid::H};
  e.set_extra_keys(extra, 8);  // more than the report, and more than the engine keeps
  TEST_ASSERT_TRUE(e.hid().has_key(hid::A));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::UP));     // 3 held + 3 extras
  TEST_ASSERT_FALSE(e.hid().has_key(hid::LEFT));  // the 7th is dropped
  TEST_ASSERT_EQUAL_HEX8(MOD_LCTRL, e.hid().mods);
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mouse_buttons);
  e.process(release(0, 0, 4));
  TEST_ASSERT_FALSE(e.hid().has_key(hid::A));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::LEFT));  // a freed slot goes to the next one
  TEST_ASSERT_FALSE(e.hid().has_key(hid::RIGHT));
}

void test_second_key_with_a_held_usage_is_pressed_again() {
  Host h;
  Engine e(h);
  e.process(press(3, 2, 0));  // C, held
  e.process(press(1, 0, 1));  // Ctrl+C rolled onto it: the host must see C go up and down again
  TEST_ASSERT_FALSE(e.hid().has_key(hid::C));
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mods);
  e.tick(1 + Engine::TAP_MS - 1);
  TEST_ASSERT_FALSE(e.hid().has_key(hid::C));
  e.tick(1 + Engine::TAP_MS);
  TEST_ASSERT_TRUE(e.hid().has_key(hid::C));
  TEST_ASSERT_EQUAL_HEX8(MOD_LCTRL, e.hid().mods);
  const HidState both = e.hid();
  e.process(release(3, 2, 20));  // the older key goes: nothing changes for the host
  TEST_ASSERT_TRUE(e.hid() == both);
  e.process(release(1, 0, 21));
  TEST_ASSERT_TRUE(e.hid() == HidState{});
}

void test_second_key_gap_waits_for_usb_delivery() {
  Host h;
  Engine e(h);
  e.process(press(3, 2, 0));  // C, held
  e.process(press(1, 0, 1));  // Ctrl+C
  e.tick(50, false);          // the report without C has not left yet
  TEST_ASSERT_FALSE(e.hid().has_key(hid::C));
  e.tick(50 + Engine::TAP_MS - 1);
  TEST_ASSERT_FALSE(e.hid().has_key(hid::C));
  e.tick(50 + Engine::TAP_MS);
  TEST_ASSERT_TRUE(e.hid().has_key(hid::C));
}

void test_seventh_shortcut_brings_no_modifiers_without_its_key() {
  Host h;
  Engine e(h);
  e.process(press(0, 0, 0));  // A
  e.process(press(3, 1, 1));  // B
  e.process(press(3, 0, 2));  // FN, held by the next key
  e.process(press(1, 1, 3));  // F3
  e.process(press(1, 2, 4));  // F4
  e.process(press(2, 0, 5));  // F5
  e.process(press(2, 1, 6));  // F6: the report is full
  e.process(press(1, 0, 7));  // Ctrl+C, the 7th
  TEST_ASSERT_FALSE(e.hid().has_key(hid::C));
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mods);  // not Ctrl on the six older keys
  e.process(release(0, 0, 8));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::C));
  TEST_ASSERT_EQUAL_HEX8(MOD_LCTRL, e.hid().mods);
}

void test_repeated_and_out_of_range_events_are_ignored() {
  Host h;
  Engine e(h);
  e.process(press(ROWS, 0, 0));
  e.process(press(0, COLS, 0));
  TEST_ASSERT_TRUE(e.hid() == HidState{});
  e.process(press(0, 0, 1));
  e.process(press(0, 0, 2));  // the same press again must not be held twice
  e.process(release(0, 0, 3));
  TEST_ASSERT_TRUE(e.hid() == HidState{});
  e.process(release(0, 0, 4));
  e.process(press(0, 0, 5));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::A));
  e.process(release(0, 0, 6));
  TEST_ASSERT_TRUE(e.hid() == HidState{});
  e.process(press(1, 2, 7));  // TG(1)
  e.process(press(1, 2, 8));  // not toggled back
  TEST_ASSERT_EQUAL(1, e.top_layer());
}

void test_two_holders_of_one_momentary_layer() {
  Host h;
  Engine e(h);
  e.set_base_layer(1);
  e.process(press(3, 0, 0));  // TH(0): hold = MO(2)
  e.tick(200);
  TEST_ASSERT_EQUAL(2, e.top_layer());
  e.process(press(1, 0, 210));  // TH(1) from SECOND: hold = MO(2) too
  e.tick(410);
  e.process(release(3, 0, 420));
  TEST_ASSERT_EQUAL(2, e.top_layer());  // the other holder keeps FN on
  e.process(press(0, 0, 430));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::F1));
  e.process(release(0, 0, 440));
  e.process(release(1, 0, 450));
  TEST_ASSERT_EQUAL(1, e.top_layer());
}

void test_hold_term_across_millis_wrap() {
  Host h;
  Engine e(h);
  const uint32_t t0 = 0xFFFFFFF0u;  // t0 + 200 wraps
  e.process(press(3, 0, t0));
  e.tick(t0 + 10);  // still before the wrap
  TEST_ASSERT_EQUAL(0, e.top_layer());
  e.tick(t0 + 199);
  TEST_ASSERT_EQUAL(0, e.top_layer());
  e.tick(t0 + 200);
  TEST_ASSERT_EQUAL(2, e.top_layer());
}

void test_tapped_key_stays_down_10_ms_across_millis_wrap() {
  Host h;
  Engine e(h);
  e.set_base_layer(1);
  const uint32_t t0 = 0xFFFFFFFAu;  // t0 + 10 wraps
  e.process(press(1, 0, t0 - 2));   // TH(1): tap = Esc
  e.process(release(1, 0, t0));
  e.tick(t0 + 5);  // still before the wrap
  TEST_ASSERT_TRUE(e.hid().has_key(hid::ESC));
  e.tick(t0 + 9);
  TEST_ASSERT_TRUE(e.hid().has_key(hid::ESC));
  e.tick(t0 + 10);
  TEST_ASSERT_FALSE(e.hid().has_key(hid::ESC));
}

static int count_key_downs(Engine& e, uint32_t from, uint32_t ms, std::vector<uint8_t>* usages = nullptr) {
  int downs = 0;
  uint8_t prev = e.hid().keys[0];
  for (uint32_t i = 0; i < ms; i++) {
    e.tick(from + i);
    const uint8_t now = e.hid().keys[0];
    if (now && !prev) {
      downs++;
      if (usages) usages->push_back(now);
    }
    prev = now;
  }
  return downs;
}

void test_macro_keeps_its_pace_across_millis_wrap() {
  Host h;
  Engine e(h);
  const uint32_t t0 = 0xFFFFFFF0u;  // the second character starts after the wrap
  e.process(press(1, 1, t0));
  TEST_ASSERT_EQUAL(3, count_key_downs(e, t0, 40));  // "Hi!": 8 ms down, 8 ms up
  TEST_ASSERT_TRUE(e.macro_running());               // t0 + 39: the last release is due at t0 + 40
  e.tick(t0 + 40);
  TEST_ASSERT_FALSE(e.macro_running());
}

void test_macro_key_pressed_again_does_not_restart_the_text() {
  Host h;
  Engine e(h);
  e.process(press(1, 1, 0));
  e.process(release(1, 1, 5));
  int downs = count_key_downs(e, 0, 20);
  e.process(press(1, 1, 20));  // mid-text
  e.process(release(1, 1, 25));
  downs += count_key_downs(e, 20, 200);
  TEST_ASSERT_EQUAL(3, downs);  // "Hi!" once
  TEST_ASSERT_FALSE(e.macro_running());
}

void test_macro_skips_a_character_without_a_key() {
  Host h;
  Engine e(h);
  e.queue_tap(MACRO(1), 0);  // "a\x01b"
  std::vector<uint8_t> typed;
  count_key_downs(e, 0, 100, &typed);
  TEST_ASSERT_EQUAL(2, typed.size());
  TEST_ASSERT_EQUAL_HEX8(hid::A, typed[0]);
  TEST_ASSERT_EQUAL_HEX8(hid::B, typed[1]);
  TEST_ASSERT_FALSE(e.macro_running());
}

void test_layer_next_stays_put_when_every_layer_is_skipped() {
  Host h;
  Engine e(h);
  e.set_skip_layers(0b011);
  e.process(press(3, 0, 0));
  e.process(release(3, 0, 50));  // tap = LNEXT: must return
  TEST_ASSERT_EQUAL(0, e.base_layer());
}

void test_wheel_steps_beyond_one_report_carry_over() {
  Host h;
  Engine e(h);
  e.queue_tap(WHEEL(100), 0);
  e.queue_tap(WHEEL(100), 0);
  int8_t wheel = 0, pan = 0;
  e.take_wheel(wheel, pan);
  TEST_ASSERT_EQUAL(127, wheel);
  e.take_wheel(wheel, pan);
  TEST_ASSERT_EQUAL(73, wheel);
  e.take_wheel(wheel, pan);
  TEST_ASSERT_EQUAL(0, wheel);
  TEST_ASSERT_EQUAL(0, pan);
}

void test_two_mouse_buttons_are_held_together() {
  Host h;
  Engine e(h);
  e.set_encoder_presets(PRESETS, 3);
  e.set_enc_preset(0, 1);  // SCROLL: press = middle button
  e.encoder_button(true, 0);
  e.process(press(0, 2, 1));  // left button
  TEST_ASSERT_EQUAL_HEX8(MB_LEFT | MB_MIDDLE, e.hid().mouse_buttons);
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

void test_knob_press_resolves_pending_layer_hold() {
  Host h;
  Engine e(h);
  e.process(press(3, 0, 0));   // LAYER key down, tap-hold undecided
  e.encoder_button(true, 20);  // pressing the knob resolves it as FN
  TEST_ASSERT_EQUAL(2, e.top_layer());
  TEST_ASSERT_TRUE(e.hid().has_key(hid::ENTER));
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);  // not the base layer's MUTE
  e.encoder_button(false, 40);
  TEST_ASSERT_FALSE(e.hid().has_key(hid::ENTER));
  e.process(release(3, 0, 50));
  e.tick(70);
  TEST_ASSERT_EQUAL(0, e.base_layer());  // no LNEXT tap from that hold
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

void test_knob_turn_while_pressed_does_not_repeat_the_press() {
  Host h;
  Engine e(h);
  e.encoder_button(true, 0);
  TEST_ASSERT_EQUAL_HEX16(cc::MUTE, e.hid().consumer);
  e.encoder_step(+1, 5);
  TEST_ASSERT_EQUAL_HEX16(cc::VOL_UP, e.hid().consumer);
  for (uint32_t t = 6; t < 40; t++) {
    e.tick(t);
    TEST_ASSERT_NOT_EQUAL(cc::MUTE, e.hid().consumer);  // one usage per report: MUTE again = a second mute
  }
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);
  e.encoder_button(false, 40);
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);
}

void test_knob_turns_while_host_asleep_are_not_replayed() {
  Host h;
  Engine e(h);
  for (uint32_t t = 0; t < 200; t += 10) {  // 20 detents, nobody listening
    e.tick(t, false);
    e.encoder_step(+1, t);
    e.clear_queued_taps();
  }
  TEST_ASSERT_EQUAL_HEX16(cc::VOL_UP, e.hid().consumer);  // the first one stays down: it requests wake-up
  int presses = 0;
  uint16_t prev = e.hid().consumer;
  for (uint32_t t = 200; t < 1200; t++) {
    e.tick(t);
    if (e.hid().consumer && !prev) presses++;
    prev = e.hid().consumer;
  }
  TEST_ASSERT_EQUAL(0, presses);
  TEST_ASSERT_EQUAL_HEX16(0, e.hid().consumer);
  TEST_ASSERT_EQUAL(20, e.encoder_position());
}

void test_knob_taps_wait_for_a_running_macro() {
  Host h;
  Engine e(h);
  e.process(press(1, 1, 0));  // MACRO(0): "Hi!"
  e.process(release(1, 1, 5));
  int ctrl_c_reports = 0;
  HidState last{};
  for (uint32_t t = 6; t < 300; t++) {
    if (t == 12) e.queue_tap(KC(MOD_LCTRL, hid::C), t);  // a detent while the text is going out
    e.tick(t);
    const HidState& s = e.hid();
    if (!(s.mods & MOD_LCTRL)) {
      last = s;
      continue;
    }
    TEST_ASSERT_EQUAL_HEX8(hid::C, s.keys[0]);  // Ctrl never lands on a macro character
    TEST_ASSERT_EQUAL_HEX8(0, s.keys[1]);
    TEST_ASSERT_FALSE(e.macro_running());
    if (s != last) ctrl_c_reports++;
    last = s;
  }
  TEST_ASSERT_EQUAL(1, ctrl_c_reports);  // delayed, not dropped
}

void test_keys_pressed_during_a_macro_wait_for_it() {
  Host h;
  Engine e(h);
  e.process(press(1, 1, 0));  // MACRO(0): "Hi!"
  e.process(release(1, 1, 5));
  std::string typed;
  int ctrl_c_reports = 0;
  HidState last{};
  for (uint32_t t = 6; t < 300; t++) {
    if (t == 10) e.process(press(1, 0, t));  // Ctrl+C while the text is going out
    if (t == 200) e.process(release(1, 0, t));
    e.tick(t);
    const HidState& s = e.hid();
    if (s == last) continue;
    if (e.macro_running() && s.keys[0]) {
      TEST_ASSERT_EQUAL_HEX8(0, s.mods & MOD_LCTRL);  // Ctrl never lands on a macro character
      TEST_ASSERT_EQUAL_HEX8(0, s.keys[1]);
      typed += s.keys[0] == hid::H ? 'H' : s.keys[0] == hid::I ? 'i' : s.keys[0] == hid::N1 ? '!' : '?';
    } else if (s.mods & MOD_LCTRL) {
      TEST_ASSERT_EQUAL_HEX8(hid::C, s.keys[0]);
      ctrl_c_reports++;
    }
    last = s;
  }
  TEST_ASSERT_EQUAL_STRING("Hi!", typed.c_str());
  TEST_ASSERT_EQUAL(1, ctrl_c_reports);  // still held when the text ends: pressed then, not dropped
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().keys[0]);
}

void test_stick_keys_wait_for_a_macro() {
  Host h;
  Engine e(h);
  const uint8_t up = hid::UP;
  e.set_extra_keys(&up, 1);  // the joystick edition's arrows
  e.process(press(1, 1, 0));  // MACRO(0): "Hi!"
  for (uint32_t t = 0; t < 300 && e.macro_running(); t++) {
    e.tick(t);
    if (e.macro_running()) TEST_ASSERT_FALSE(e.hid().has_key(hid::UP));  // Shift+Up would select text
  }
  TEST_ASSERT_FALSE(e.macro_running());
  TEST_ASSERT_TRUE(e.hid().has_key(hid::UP));
}

void test_macro_waits_for_a_knob_tap_that_is_down() {
  Host h;
  Engine e(h);
  e.queue_tap(KC(MOD_LCTRL, hid::C), 10);  // the tap is down from t=10
  e.process(press(1, 1, 12));              // MACRO(0): "Hi!" while it is down
  e.process(release(1, 1, 14));
  for (uint32_t t = 12; t < 300; t++) {
    e.tick(t);
    const HidState& s = e.hid();
    if (!(s.mods & MOD_LCTRL)) continue;
    TEST_ASSERT_EQUAL_HEX8(hid::C, s.keys[0]);  // Ctrl only ever with the tap's own key
    TEST_ASSERT_EQUAL_HEX8(0, s.keys[1]);
  }
  TEST_ASSERT_FALSE(e.macro_running());  // delayed, not dropped
}

void test_macro_drops_knob_taps_queued_before_it() {
  Host h;
  Engine e(h);
  for (int i = 0; i < 3; i++) e.queue_tap(KC(MOD_LCTRL, hid::C), 10);  // one down, two waiting
  e.process(press(1, 1, 12));  // MACRO(0): "Hi!"
  e.process(release(1, 1, 14));
  bool typed = false;
  for (uint32_t t = 12; t < 300; t++) {
    e.tick(t);
    if (e.macro_running()) {
      typed = true;
    } else if (typed) {
      TEST_ASSERT_EQUAL_HEX8(0, e.hid().mods);  // an old Ctrl+Z detent would undo the text
    }
  }
  TEST_ASSERT_TRUE(typed);
}

void test_key_press_ends_a_knob_tap_with_modifiers() {
  Host h;
  Engine e(h);
  e.set_encoder_presets(PRESETS, 3);
  e.set_enc_preset(0, 2);  // ZOOM: Ctrl+= / Ctrl+-
  e.encoder_step(+1, 0);
  e.encoder_step(+1, 0);
  TEST_ASSERT_EQUAL_HEX8(MOD_LCTRL, e.hid().mods);
  TEST_ASSERT_TRUE(e.hid().has_key(hid::EQUAL));
  e.process(press(0, 0, 2));  // 'a' while the first tap is down
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mods);  // not Ctrl+A
  TEST_ASSERT_TRUE(e.hid().has_key(hid::A));
  TEST_ASSERT_FALSE(e.hid().has_key(hid::EQUAL));
  e.process(release(0, 0, 4));
  e.tick(7);
  TEST_ASSERT_EQUAL_HEX8(0, e.hid().mods);  // the gap is kept
  e.tick(8);
  TEST_ASSERT_EQUAL_HEX8(MOD_LCTRL, e.hid().mods);  // the second detent still goes out
  TEST_ASSERT_TRUE(e.hid().has_key(hid::EQUAL));
}

void test_key_press_leaves_a_knob_tap_without_modifiers() {
  Host h;
  Engine e(h);
  e.queue_tap(K(hid::RIGHT), 0);
  e.process(press(0, 0, 2));
  TEST_ASSERT_TRUE(e.hid().has_key(hid::RIGHT));  // full length: nothing to keep off the key
  TEST_ASSERT_TRUE(e.hid().has_key(hid::A));
}

void test_fn_layer_is_never_the_base_layer() {
  Host h;
  Engine e(h);
  e.set_base_layer(1);
  e.set_base_layer(FN_LAYER);  // console "layer <n>" or a bad saved byte
  TEST_ASSERT_EQUAL(1, e.base_layer());
}

void test_knob_cycle_on_a_base_layer_without_its_own_knob() {
  Host h;
  Engine e(h);
  e.set_encoder_presets(PRESETS, 3);
  e.set_base_layer(1);  // SECOND: CENTRE_INHERIT, and nothing below a base layer
  TEST_ASSERT_EQUAL_STRING("VOLUME", e.encoder_binding().label);  // falls back to the first preset
  TEST_ASSERT_EQUAL(1, e.cycle_enc_preset(1));  // not preset 0: that is what the knob already does
  TEST_ASSERT_EQUAL_STRING("SCROLL", e.encoder_binding().label);
  TEST_ASSERT_EQUAL(2, e.cycle_enc_preset(1));
  TEST_ASSERT_EQUAL_STRING("ZOOM", e.encoder_binding().label);
  TEST_ASSERT_EQUAL(ENC_LAYER_DEFAULT, e.cycle_enc_preset(1));
  TEST_ASSERT_EQUAL_STRING("VOLUME", e.encoder_binding().label);
}

// -------------------------------------------------------------------- LEDs
void test_led_layer_colour_and_flash() {
  LedInput in;
  in.color565 = 0xF800;  // red
  Rgb px[16];
  led_frame(in, 0, px, 16);
  const uint8_t rest = px[0].r;
  TEST_ASSERT_EQUAL(141, rest);  // 190 through the gamma-2 curve, not 190
  TEST_ASSERT_EQUAL(0, px[0].g);
  TEST_ASSERT_EQUAL(0, px[15].b);
  in.since_hit_ms = 0;
  led_frame(in, 0, px, 16);
  TEST_ASSERT_TRUE(px[0].r > rest);  // flash on a key press
  in.since_hit_ms = LED_PULSE_MS / 2;
  led_frame(in, 0, px, 16);
  TEST_ASSERT_TRUE(px[0].r > rest && px[0].r < 255);  // that fades
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
  in.color565 = 0x07E0;  // green over the budget stays green
  in.budget_ma = 100;
  led_frame(in, 0, px, 16);
  TEST_ASSERT_TRUE(px[0].r == 0 && px[0].g > 100 && px[0].b == 0);
  TEST_ASSERT_TRUE(led_current_ma(px, 16) <= 100);
  in.color565 = 0xF800;
  in.budget_ma = 0;
  in.max_level = 128;  // the brightness cap
  led_frame(in, 0, px, 16);
  TEST_ASSERT_EQUAL(71, px[0].r);
  in.max_level = 255;
  in.level = 128;  // and the backlight level dim alike
  led_frame(in, 0, px, 16);
  TEST_ASSERT_EQUAL(71, px[0].r);
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
  TEST_ASSERT_EQUAL(6, a[0].b);  // the trough: 40 through the gamma-2 curve
  Rgb falling[1], next_period[1];
  led_frame(in, LED_BREATHE_MS * 3 / 4, falling, 1);
  TEST_ASSERT_TRUE(falling[0].b > a[0].b && falling[0].b < b[0].b);
  led_frame(in, LED_BREATHE_MS, next_period, 1);
  TEST_ASSERT_EQUAL(a[0].b, next_period[0].b);
  TEST_ASSERT_EQUAL_STRING("LAYER", led_mode_name(LedMode::Layer));
  TEST_ASSERT_EQUAL_STRING("BREATHE", led_mode_name(LedMode::Breathe));
  TEST_ASSERT_EQUAL_STRING("RAINBOW", led_mode_name(LedMode::Rainbow));
  TEST_ASSERT_EQUAL_STRING("OFF", led_mode_name(LedMode::Off));
}

static void expect_rgb(uint8_t r, uint8_t g, uint8_t b, Rgb actual) {
  TEST_ASSERT_EQUAL(r, actual.r);
  TEST_ASSERT_EQUAL(g, actual.g);
  TEST_ASSERT_EQUAL(b, actual.b);
}

void test_led_colour_conversions() {
  expect_rgb(255, 0, 0, hsv(0, 255, 255));
  expect_rgb(0, 255, 0, hsv(512, 255, 255));
  expect_rgb(0, 0, 255, hsv(1024, 255, 255));
  expect_rgb(127, 255, 0, hsv(384, 255, 255));  // mid-sector: yellow-green, then azure
  expect_rgb(0, 127, 255, hsv(896, 255, 255));
  expect_rgb(255, 0, 0, hsv(1536, 255, 255));  // the wheel wraps (Rainbow passes hues past one turn)
  expect_rgb(0, 130, 0, rgb565_to_rgb(0x0400));  // green has 6 bits
  expect_rgb(255, 255, 255, rgb565_to_rgb(0xFFFF));
}

// ------------------------------------------------------------- host bridge
void test_host_action_reports_press_and_release() {
  Host h;
  Engine e(h);
  e.process(press(1, 2, 0));  // TG(1)
  e.process(release(1, 2, 5));
  e.process(press(2, 0, 10));
  e.process(release(2, 0, 20));
  TEST_ASSERT_EQUAL(2, int(h.host.size()));
  TEST_ASSERT_EQUAL(7 * 2 + 1, h.host[0]);
  TEST_ASSERT_EQUAL(7 * 2, h.host[1]);
  TEST_ASSERT_EQUAL(0, e.hid().keys[0]);  // nothing for the USB keyboard
}

void test_layer_next_skips_masked_layers() {
  Host h;
  Engine e(h);
  e.set_skip_layers(1u << 1);  // e.g. a host layer while no daemon is connected
  e.process(press(3, 0, 0));
  e.process(release(3, 0, 50));
  TEST_ASSERT_EQUAL(0, e.base_layer());  // 1 skipped, 2 is FN: stays
  e.set_skip_layers(0);
  e.tick(100);
  e.process(press(3, 0, 200));
  e.process(release(3, 0, 250));
  TEST_ASSERT_EQUAL(1, e.base_layer());
}

static std::vector<uint8_t> pkt(std::initializer_list<uint8_t> head) {
  std::vector<uint8_t> p(HOST_PACKET, 0);
  size_t i = 0;
  for (uint8_t b : head) p[i++] = b;
  return p;
}
static void put32(std::vector<uint8_t>& p, int at, uint32_t v) {
  for (int i = 0; i < 4; i++) p[at + i] = uint8_t(v >> (8 * i));
}

void test_host_hello_links_and_acks() {
  HostProto hp;
  hp.set_identity(2, 5, "Nyxilab rotary");
  uint8_t out[HOST_PACKET];
  TEST_ASSERT_FALSE(hp.linked());
  hp.send_key(1, true);  // not linked: dropped
  TEST_ASSERT_FALSE(hp.next_tx(out));
  // UTF-8 and control characters are not printable here
  auto hello = pkt({PKT_HELLO, PROTO_VERSION, 'd', 'e', 's', 'k', 0xC3, 0xA9, 0x0A, 0x09, 0x7F, 0x1F});
  TEST_ASSERT_TRUE(hp.on_packet(hello.data(), uint16_t(hello.size()), 1000));
  TEST_ASSERT_TRUE(hp.linked());
  TEST_ASSERT_EQUAL_STRING("desk??????", hp.data().hostname);
  TEST_ASSERT_TRUE(hp.take_state_request());
  TEST_ASSERT_FALSE(hp.take_state_request());
  TEST_ASSERT_TRUE(hp.next_tx(out));
  TEST_ASSERT_EQUAL_HEX8(PKT_HELLO_ACK, out[0]);
  TEST_ASSERT_EQUAL(PROTO_VERSION, out[1]);
  TEST_ASSERT_EQUAL(2, out[2]);
  TEST_ASSERT_EQUAL(5, out[3]);
  TEST_ASSERT_EQUAL(PROTO_VERSION, out[4]);
  TEST_ASSERT_EQUAL_STRING("Nyxilab rotary", reinterpret_cast<const char*>(out + 5));
  TEST_ASSERT_FALSE(hp.next_tx(out));
}

void test_host_metrics_text_and_timeout() {
  HostProto hp;
  auto hello = pkt({PKT_HELLO, PROTO_VERSION, 'h'});
  hp.on_packet(hello.data(), HOST_PACKET, 0);
  // byte layout of host/src/protocol.rs metrics(); host/tests/interop.rs runs both together
  auto m = pkt({PKT_METRICS, PROTO_VERSION, 37, 4, 10, 20, 30, 250});
  put32(m, 20, 6144);
  put32(m, 24, 16384);
  m[28] = 5;
  m[29] = 61;
  m[30] = 0xC5; m[31] = 0x01;  // 45.3 degC
  m[32] = 0x7B; m[33] = 0x00;  // load 1.23
  put32(m, 34, 1500000);
  put32(m, 38, 2048);
  put32(m, 42, 93784);
  m[46] = 0xFF;
  m[47] = 0x00; m[48] = 0x80;  // no GPU temperature
  m[49] = 14; m[50] = 7;
  TEST_ASSERT_TRUE(hp.on_packet(m.data(), HOST_PACKET, 1000));
  const HostData& d = hp.data();
  TEST_ASSERT_TRUE(d.has_metrics);
  TEST_ASSERT_EQUAL(37, d.m.cpu_pct);
  TEST_ASSERT_EQUAL(4, d.m.n_cores);
  TEST_ASSERT_EQUAL(30, d.m.core_pct[2]);
  TEST_ASSERT_EQUAL(HOST_UNKNOWN, d.m.core_pct[3]);  // > 100 is not a percentage
  TEST_ASSERT_EQUAL(37, d.m.mem_pct());
  TEST_ASSERT_EQUAL(453, d.m.cpu_temp_c10);
  TEST_ASSERT_EQUAL(123, d.m.load1_x100);
  TEST_ASSERT_EQUAL_UINT32(1500000, d.m.net_rx_bps);
  TEST_ASSERT_EQUAL_UINT32(93784, d.m.uptime_s);
  TEST_ASSERT_EQUAL(HOST_UNKNOWN, d.m.gpu_pct);
  TEST_ASSERT_EQUAL(HOST_NO_TEMP, d.m.gpu_temp_c10);
  TEST_ASSERT_EQUAL(14, d.m.clock_h);
  TEST_ASSERT_EQUAL(37, d.cpu_hist[HOST_HISTORY - 1]);
  TEST_ASSERT_EQUAL(37, d.mem_hist[HOST_HISTORY - 1]);

  auto t = pkt({PKT_TEXT, PROTO_VERSION, 2, 'b', 'u', 'i', 'l', 'd', ' ', 'o', 'k'});
  TEST_ASSERT_TRUE(hp.on_packet(t.data(), HOST_PACKET, 1500));
  TEST_ASSERT_EQUAL_STRING("build ok", d.text[2]);
  auto bad_slot = pkt({PKT_TEXT, PROTO_VERSION, 9, 'x'});
  TEST_ASSERT_FALSE(hp.on_packet(bad_slot.data(), HOST_PACKET, 1500));
  auto other_version = pkt({PKT_TEXT, 2, 1, 'x'});
  TEST_ASSERT_FALSE(hp.on_packet(other_version.data(), HOST_PACKET, 1500));
  TEST_ASSERT_EQUAL_STRING("", d.text[1]);

  hp.tick(1500 + HOST_TIMEOUT_MS - 1);
  TEST_ASSERT_TRUE(hp.linked());
  hp.tick(1500 + HOST_TIMEOUT_MS);
  TEST_ASSERT_FALSE(hp.linked());  // silence: back to "waiting for host"
  TEST_ASSERT_FALSE(d.has_metrics);
  TEST_ASSERT_EQUAL_STRING("", d.text[2]);
}

void test_host_packet_limits() {
  HostProto hp;
  const HostData& d = hp.data();
  std::vector<uint8_t> hello(HOST_PACKET, 'a');  // no NUL anywhere: the copy must stop at the buffer
  hello[0] = PKT_HELLO;
  hello[1] = PROTO_VERSION;
  TEST_ASSERT_TRUE(hp.on_packet(hello.data(), HOST_PACKET, 0));
  TEST_ASSERT_EQUAL(HOST_HOSTNAME_LEN, strlen(d.hostname));
  TEST_ASSERT_EQUAL(HOST_UNKNOWN, d.m.cpu_pct);
  TEST_ASSERT_EQUAL(0, d.m.n_cores);
  std::vector<uint8_t> text(HOST_PACKET, 'b');
  text[0] = PKT_TEXT;
  text[1] = PROTO_VERSION;
  text[2] = 0;
  TEST_ASSERT_TRUE(hp.on_packet(text.data(), HOST_PACKET, 0));
  TEST_ASSERT_EQUAL(HOST_TEXT_LEN, strlen(d.text[0]));
  TEST_ASSERT_EQUAL_STRING("", d.text[1]);

  auto m = pkt({PKT_METRICS, PROTO_VERSION, 40, 200});  // 200 core bars: the renderer indexes core_pct by n_cores
  for (int i = 4; i < 20; i++) m[i] = 50;
  put32(m, 20, 1000);
  put32(m, 24, 2000);
  m[49] = 24;
  m[50] = 60;
  TEST_ASSERT_TRUE(hp.on_packet(m.data(), HOST_PACKET, 0));
  TEST_ASSERT_EQUAL(HOST_MAX_CORES, d.m.n_cores);
  TEST_ASSERT_EQUAL(HOST_UNKNOWN, d.m.clock_h);
  TEST_ASSERT_EQUAL(HOST_UNKNOWN, d.m.clock_m);
  m[2] = 0xFF;  // CPU not measured, more memory used than there is
  put32(m, 20, 5000);
  TEST_ASSERT_TRUE(hp.on_packet(m.data(), HOST_PACKET, 0));
  TEST_ASSERT_EQUAL(100, d.m.mem_pct());
  TEST_ASSERT_EQUAL(0, d.cpu_hist[HOST_HISTORY - 1]);
  TEST_ASSERT_EQUAL(100, d.mem_hist[HOST_HISTORY - 1]);
  TEST_ASSERT_EQUAL(40, d.cpu_hist[HOST_HISTORY - 2]);  // the older sample moved along
  TEST_ASSERT_EQUAL(50, d.mem_hist[HOST_HISTORY - 2]);
}

void test_host_timeout_across_millis_wrap() {
  HostProto hp;
  const uint32_t t0 = 0xFFFFF000u;
  auto hello = pkt({PKT_HELLO, PROTO_VERSION});
  hp.on_packet(hello.data(), HOST_PACKET, t0);
  hp.tick(t0 + 1);
  TEST_ASSERT_TRUE(hp.linked());  // not dropped early because t0 + timeout wraps
  hp.tick(t0 + HOST_TIMEOUT_MS - 1);
  TEST_ASSERT_TRUE(hp.linked());
  hp.tick(t0 + HOST_TIMEOUT_MS);
  TEST_ASSERT_FALSE(hp.linked());
}

void test_host_relink_without_hello_asks_for_it() {
  HostProto hp;
  auto m = pkt({PKT_METRICS, PROTO_VERSION, 10});
  TEST_ASSERT_TRUE(hp.on_packet(m.data(), HOST_PACKET, 0));  // e.g. after a timeout while the host slept
  TEST_ASSERT_TRUE(hp.linked());
  uint8_t out[HOST_PACKET];
  TEST_ASSERT_TRUE(hp.next_tx(out));
  TEST_ASSERT_EQUAL_HEX8(PKT_HELLO_ACK, out[0]);  // the daemon replays HELLO + text on this
  TEST_ASSERT_TRUE(hp.take_state_request());
  TEST_ASSERT_TRUE(hp.on_packet(m.data(), HOST_PACKET, 1000));
  TEST_ASSERT_FALSE(hp.next_tx(out));  // only once
}

void test_host_key_state_queue_and_bye() {
  HostProto hp;
  auto hello = pkt({PKT_HELLO, PROTO_VERSION});
  hp.on_packet(hello.data(), HOST_PACKET, 0);
  uint8_t out[HOST_PACKET];
  TEST_ASSERT_TRUE(hp.next_tx(out));  // HELLO_ACK
  hp.send_state(true, 1, 4);
  hp.send_key(9, true);
  TEST_ASSERT_TRUE(hp.next_tx(out));
  TEST_ASSERT_EQUAL_HEX8(PKT_STATE, out[0]);
  TEST_ASSERT_EQUAL(1, out[2]);
  TEST_ASSERT_EQUAL(1, out[3]);  // base
  TEST_ASSERT_EQUAL(4, out[4]);  // top
  TEST_ASSERT_TRUE(hp.next_tx(out));
  TEST_ASSERT_EQUAL_HEX8(PKT_KEY, out[0]);
  TEST_ASSERT_EQUAL(9, out[2]);
  TEST_ASSERT_EQUAL(1, out[3]);
  for (int i = 0; i < 20; i++) hp.send_key(1, i & 1);  // more than the queue holds
  int n = 0;
  while (hp.next_tx(out)) n++;
  TEST_ASSERT_EQUAL(HostProto::TX_QUEUE, n);
  hp.send_key(2, true);
  auto bye = pkt({PKT_BYE, PROTO_VERSION});
  TEST_ASSERT_TRUE(hp.on_packet(bye.data(), HOST_PACKET, 10));
  TEST_ASSERT_FALSE(hp.linked());
  TEST_ASSERT_FALSE(hp.next_tx(out));  // pending events dropped with the link
}

void test_host_hello_starts_with_an_empty_queue() {
  HostProto hp;
  auto hello = pkt({PKT_HELLO, PROTO_VERSION});
  hp.on_packet(hello.data(), HOST_PACKET, 0);
  uint8_t out[HOST_PACKET];
  TEST_ASSERT_TRUE(hp.next_tx(out));  // HELLO_ACK
  TEST_ASSERT_TRUE(hp.take_state_request());
  for (int i = 0; i < HostProto::TX_QUEUE; i++) hp.send_key(1, i & 1);  // HOST taps while the daemon is dead
  TEST_ASSERT_TRUE(hp.on_packet(hello.data(), HOST_PACKET, 2000));     // restarted inside the timeout
  TEST_ASSERT_TRUE(hp.take_state_request());
  TEST_ASSERT_TRUE(hp.next_tx(out));
  TEST_ASSERT_EQUAL_HEX8(PKT_HELLO_ACK, out[0]);
  TEST_ASSERT_FALSE(hp.next_tx(out));  // the new daemon does not run the old session's keys
}

void test_host_timeout_drops_queued_events() {
  HostProto hp;
  auto hello = pkt({PKT_HELLO, PROTO_VERSION});
  hp.on_packet(hello.data(), HOST_PACKET, 0);
  uint8_t out[HOST_PACKET];
  TEST_ASSERT_TRUE(hp.next_tx(out));  // HELLO_ACK
  hp.send_key(1, true);               // HOST tap the sleeping host never took
  hp.send_key(1, false);
  hp.tick(HOST_TIMEOUT_MS);
  TEST_ASSERT_FALSE(hp.linked());
  auto m = pkt({PKT_METRICS, PROTO_VERSION, 10});
  TEST_ASSERT_TRUE(hp.on_packet(m.data(), HOST_PACKET, HOST_TIMEOUT_MS + 100));  // host woke, no HELLO
  TEST_ASSERT_TRUE(hp.next_tx(out));
  TEST_ASSERT_EQUAL_HEX8(PKT_HELLO_ACK, out[0]);
  TEST_ASSERT_FALSE(hp.next_tx(out));  // the stale keys are not run on wake
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_debounce_filters_bounce);
  RUN_TEST(test_debounce_restarts_timer_after_a_spike);
  RUN_TEST(test_debounce_release_keeps_other_keys_of_the_row);
  RUN_TEST(test_debounce_across_millis_wrap);
  RUN_TEST(test_plain_key_and_modifier);
  RUN_TEST(test_shortcut_modifiers_stay_off_a_later_key);
  RUN_TEST(test_bare_modifier_joins_a_held_shortcut);
  RUN_TEST(test_consumer_and_mouse_button);
  RUN_TEST(test_tap_goes_to_next_layer_skipping_fn);
  RUN_TEST(test_hold_timeout_activates_fn);
  RUN_TEST(test_hold_on_other_key_press);
  RUN_TEST(test_transparent_falls_through);
  RUN_TEST(test_to_layer_from_fn);
  RUN_TEST(test_toggle_layer);
  RUN_TEST(test_layer_actions_ignore_undefined_layers);
  RUN_TEST(test_momentary_layer_preserves_toggle);
  RUN_TEST(test_to_clears_toggle_but_keeps_momentary_layer);
  RUN_TEST(test_generated_taps_wait_for_usb_delivery);
  RUN_TEST(test_macro_waits_for_usb_delivery);
  RUN_TEST(test_repeated_key_taps_while_host_asleep_are_released);
  RUN_TEST(test_key_release_uses_press_layer);
  RUN_TEST(test_sys_and_centre_callbacks);
  RUN_TEST(test_macro_types_text);
  RUN_TEST(test_ascii_mapping);
  RUN_TEST(test_report_holds_six_keys_and_one_of_each_usage);
  RUN_TEST(test_second_key_with_a_held_usage_is_pressed_again);
  RUN_TEST(test_second_key_gap_waits_for_usb_delivery);
  RUN_TEST(test_seventh_shortcut_brings_no_modifiers_without_its_key);
  RUN_TEST(test_repeated_and_out_of_range_events_are_ignored);
  RUN_TEST(test_two_holders_of_one_momentary_layer);
  RUN_TEST(test_hold_term_across_millis_wrap);
  RUN_TEST(test_tapped_key_stays_down_10_ms_across_millis_wrap);
  RUN_TEST(test_macro_keeps_its_pace_across_millis_wrap);
  RUN_TEST(test_macro_key_pressed_again_does_not_restart_the_text);
  RUN_TEST(test_macro_skips_a_character_without_a_key);
  RUN_TEST(test_layer_next_stays_put_when_every_layer_is_skipped);
  RUN_TEST(test_wheel_steps_beyond_one_report_carry_over);
  RUN_TEST(test_two_mouse_buttons_are_held_together);
  RUN_TEST(test_knob_taps_consumer_keys_one_after_another);
  RUN_TEST(test_knob_press_is_held_and_latched);
  RUN_TEST(test_knob_fn_layer_binding_and_hold_on_turn);
  RUN_TEST(test_knob_press_resolves_pending_layer_hold);
  RUN_TEST(test_knob_transparent_layer_falls_through);
  RUN_TEST(test_knob_cycle_skips_duplicates_and_notifies);
  RUN_TEST(test_knob_wheel_steps_and_preset_restore);
  RUN_TEST(test_knob_queue_overflow_drops_steps);
  RUN_TEST(test_knob_turn_while_pressed_does_not_repeat_the_press);
  RUN_TEST(test_knob_turns_while_host_asleep_are_not_replayed);
  RUN_TEST(test_knob_taps_wait_for_a_running_macro);
  RUN_TEST(test_keys_pressed_during_a_macro_wait_for_it);
  RUN_TEST(test_stick_keys_wait_for_a_macro);
  RUN_TEST(test_macro_waits_for_a_knob_tap_that_is_down);
  RUN_TEST(test_macro_drops_knob_taps_queued_before_it);
  RUN_TEST(test_key_press_ends_a_knob_tap_with_modifiers);
  RUN_TEST(test_key_press_leaves_a_knob_tap_without_modifiers);
  RUN_TEST(test_fn_layer_is_never_the_base_layer);
  RUN_TEST(test_knob_cycle_on_a_base_layer_without_its_own_knob);
  RUN_TEST(test_led_layer_colour_and_flash);
  RUN_TEST(test_led_levels_budget_and_off);
  RUN_TEST(test_led_rainbow_and_breathe);
  RUN_TEST(test_led_colour_conversions);
  RUN_TEST(test_host_action_reports_press_and_release);
  RUN_TEST(test_layer_next_skips_masked_layers);
  RUN_TEST(test_host_hello_links_and_acks);
  RUN_TEST(test_host_metrics_text_and_timeout);
  RUN_TEST(test_host_packet_limits);
  RUN_TEST(test_host_timeout_across_millis_wrap);
  RUN_TEST(test_host_relink_without_hello_asks_for_it);
  RUN_TEST(test_host_key_state_queue_and_bye);
  RUN_TEST(test_host_hello_starts_with_an_empty_queue);
  RUN_TEST(test_host_timeout_drops_queued_events);
  return UNITY_END();
}
