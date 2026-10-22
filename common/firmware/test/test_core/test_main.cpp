// Host-side unit tests of lib/nyx_core (run: pio test -e native in common/firmware).
// Uses its own small keymap so editing an edition's keymap.cpp never breaks them.
#include <unity.h>

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
        {{K(hid::X), "x"}, {___, ""}, {MO(1), "hold1"}},
        {{TH(1), "esc"}, {___, ""}, {___, ""}},
        {{HOST(7), "h7"}, {___, ""}, {___, ""}},
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
const uint8_t NUM_TAP_HOLDS = 2;
const uint8_t NUM_MACROS = 1;
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
  auto hello = pkt({PKT_HELLO, PROTO_VERSION, 'd', 'e', 's', 'k', 0xC3, 0xA9});  // UTF-8 is not printable here
  TEST_ASSERT_TRUE(hp.on_packet(hello.data(), uint16_t(hello.size()), 1000));
  TEST_ASSERT_TRUE(hp.linked());
  TEST_ASSERT_EQUAL_STRING("desk??", hp.data().hostname);
  TEST_ASSERT_TRUE(hp.take_state_request());
  TEST_ASSERT_FALSE(hp.take_state_request());
  TEST_ASSERT_TRUE(hp.next_tx(out));
  TEST_ASSERT_EQUAL_HEX8(PKT_HELLO_ACK, out[0]);
  TEST_ASSERT_EQUAL(PROTO_VERSION, out[1]);
  TEST_ASSERT_EQUAL(2, out[2]);
  TEST_ASSERT_EQUAL(5, out[3]);
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
  hp.send_state(true, 3, 3);
  hp.send_key(9, true);
  TEST_ASSERT_TRUE(hp.next_tx(out));
  TEST_ASSERT_EQUAL_HEX8(PKT_STATE, out[0]);
  TEST_ASSERT_EQUAL(1, out[2]);
  TEST_ASSERT_EQUAL(3, out[3]);
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
  RUN_TEST(test_host_action_reports_press_and_release);
  RUN_TEST(test_layer_next_skips_masked_layers);
  RUN_TEST(test_host_hello_links_and_acks);
  RUN_TEST(test_host_metrics_text_and_timeout);
  RUN_TEST(test_host_relink_without_hello_asks_for_it);
  RUN_TEST(test_host_key_state_queue_and_bye);
  return UNITY_END();
}
