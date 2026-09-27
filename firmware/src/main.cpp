// Nyxilab macropad firmware.
//
// core 0: matrix scan -> debounce -> engine -> USB HID, joystick, settings, serial console
// core 1: both displays (rendering + SPI transfers)
#include <Arduino.h>

#include "config.h"
#include "core/debounce.h"
#include "core/engine.h"
#include "core/joystick.h"
#include "core/keymap.h"
#include "hw/matrix.h"
#include "hw/settings.h"
#include "hw/usb_hid.h"
#include "ui/display.h"
#include "ui/ui_state.h"

namespace {

using mp::JoyMode;

class App : public mp::EngineHost {
 public:
  void on_sys(mp::SysCmd cmd) override;
  void on_joy_mode(JoyMode mode) override;
};

App g_app;
mp::Debouncer g_debounce(cfg::DEBOUNCE_MS);
mp::Engine g_engine(g_app);
mp::Joystick g_joy;

uint32_t g_last_scan_us = 0, g_last_joy_ms = 0, g_last_activity_ms = 0, g_last_second_ms = 0;
uint8_t g_joy_sw_count = 0;
bool g_joy_sw = false;
uint8_t g_presses_this_second = 0;
ui::UiState g_ui;
uint8_t g_saved_base = 0xFF;

uint16_t read_adc_avg(uint8_t pin) {
  uint32_t sum = 0;
  for (int i = 0; i < 4; i++) sum += analogRead(pin);
  return uint16_t(sum / 4);
}

JoyMode effective_joy_mode() {
  auto& st = hw::settings();
  const uint8_t base = g_engine.base_layer();
  const mp::Layer& top = mp::LAYERS[g_engine.top_layer()];
  if (top.joy != JoyMode::Inherit && g_engine.top_layer() != base) return top.joy;
  const uint8_t user = st.joy_modes[base];
  if (user < uint8_t(JoyMode::Count)) return JoyMode(user);
  const JoyMode def = mp::LAYERS[base].joy;
  return def == JoyMode::Inherit ? JoyMode::Mouse : def;
}

void activity(uint32_t now) { g_last_activity_ms = now; }

void App::on_sys(mp::SysCmd cmd) {
  auto& st = hw::settings();
  const uint32_t now = millis();
  switch (cmd) {
    case mp::SYS_BOOTLOADER:
      Serial.println("rebooting into the UF2 bootloader");
      Serial.flush();
      delay(50);
      rp2040.rebootToBootloader();
      break;
    case mp::SYS_REBOOT:
      rp2040.reboot();
      break;
    case mp::SYS_BRIGHT_UP:
      st.brightness = uint8_t(min(255, st.brightness + cfg::BRIGHTNESS_STEP));
      hw::settings_touch(now);
      break;
    case mp::SYS_BRIGHT_DOWN:
      st.brightness = uint8_t(max(8, st.brightness - cfg::BRIGHTNESS_STEP));
      hw::settings_touch(now);
      break;
    case mp::SYS_JOY_CALIBRATE:
      g_joy.calibrate_center(read_adc_avg(cfg::JOY_X_PIN), read_adc_avg(cfg::JOY_Y_PIN));
      st.joy_cal = g_joy.cal();
      hw::settings_touch(now);
      break;
    case mp::SYS_DISPLAYS_TOGGLE:
      st.displays_on = !st.displays_on;
      hw::settings_touch(now);
      break;
  }
}

void App::on_joy_mode(JoyMode mode) {
  auto& st = hw::settings();
  const uint8_t base = g_engine.base_layer();
  if (mode == JoyMode::Cycle) {
    const uint8_t cur = uint8_t(effective_joy_mode());
    st.joy_modes[base] = uint8_t((cur + 1) % uint8_t(JoyMode::Count));
  } else if (uint8_t(mode) < uint8_t(JoyMode::Count)) {
    st.joy_modes[base] = uint8_t(mode);
  }
  hw::settings_touch(millis());
}

// ------------------------------------------------------------ serial console
void print_help() {
  Serial.println(F("commands: help | info | joy (raw stick for 5 s) | cal | layer <n> | boot"));
}

void console(uint32_t now) {
  static char line[32];
  static uint8_t n = 0;
  while (Serial.available()) {
    const char ch = char(Serial.read());
    if (ch != '\n' && ch != '\r') {
      if (n < sizeof(line) - 1) line[n++] = ch;
      continue;
    }
    line[n] = 0;
    n = 0;
    if (!strcmp(line, "help") || !line[0]) {
      print_help();
    } else if (!strcmp(line, "info")) {
      const auto& c = g_joy.cal();
      Serial.printf("layer base=%u top=%u mask=0x%lx joy=%u usb=%d leds=0x%02x bright=%u\n", g_engine.base_layer(),
                    g_engine.top_layer(), (unsigned long)g_engine.layer_mask(), unsigned(effective_joy_mode()),
                    hw::usb_mounted(), hw::usb_leds(), hw::settings().brightness);
      Serial.printf("joy cal centre=(%u,%u) x=[%u..%u] y=[%u..%u]\n", c.cx, c.cy, c.min_x, c.max_x, c.min_y, c.max_y);
    } else if (!strcmp(line, "joy")) {
      for (uint32_t t0 = millis(); millis() - t0 < 5000;) {
        Serial.printf("raw x=%4u y=%4u  norm x=%+.2f y=%+.2f  sw=%d\n", read_adc_avg(cfg::JOY_X_PIN),
                      read_adc_avg(cfg::JOY_Y_PIN), g_joy.raw_nx(), g_joy.raw_ny(), digitalRead(cfg::JOY_SW_PIN) == LOW);
        delay(100);
      }
    } else if (!strcmp(line, "cal")) {
      g_app.on_sys(mp::SYS_JOY_CALIBRATE);
      Serial.println("joystick centre re-calibrated");
    } else if (!strncmp(line, "layer ", 6)) {
      g_engine.set_base_layer(uint8_t(atoi(line + 6)));
    } else if (!strcmp(line, "boot")) {
      g_app.on_sys(mp::SYS_BOOTLOADER);
    } else {
      print_help();
    }
  }
  (void)now;
}

// ------------------------------------------------------------------ joystick
void joystick_task(uint32_t now) {
  if (now - g_last_joy_ms < cfg::JOY_PERIOD_MS) return;
  const float dt = (now - g_last_joy_ms) / 1000.f;
  g_last_joy_ms = now;

  const JoyMode mode = effective_joy_mode();
  mp::JoyOutput out;
  g_joy.update(read_adc_avg(cfg::JOY_X_PIN), read_adc_avg(cfg::JOY_Y_PIN), dt > 0.1f ? 0.1f : dt, mode, out);
  if (out.dx || out.dy || out.wheel || out.pan || out.n_keys) activity(now);
  hw::usb_add_mouse(out.dx, out.dy, out.wheel, out.pan);

  // stick click: debounced, meaning depends on the mode
  const bool sw_raw = digitalRead(cfg::JOY_SW_PIN) == LOW;
  if (sw_raw != g_joy_sw) {
    if (++g_joy_sw_count >= 3) {
      g_joy_sw = sw_raw;
      g_joy_sw_count = 0;
      activity(now);
    }
  } else {
    g_joy_sw_count = 0;
  }
  uint8_t keys[3];
  uint8_t nk = 0;
  for (uint8_t i = 0; i < out.n_keys; i++) keys[nk++] = out.keys[i];
  uint8_t buttons = 0;
  if (g_joy_sw) {
    if (mode == JoyMode::Mouse) buttons = mp::MB_LEFT;
    if (mode == JoyMode::Scroll) buttons = mp::MB_MIDDLE;
    if (mode == JoyMode::Arrows) keys[nk++] = mp::hid::ENTER;
  }
  hw::usb_set_extra_buttons(buttons);
  g_engine.set_extra_keys(keys, nk);
}

// ------------------------------------------------------------------ UI feed
void publish_ui(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 10) return;
  last = now;
  const auto& st = hw::settings();
  ui::UiState& u = g_ui;
  u.base_layer = g_engine.base_layer();
  u.top_layer = g_engine.top_layer();
  u.layer_mask = g_engine.layer_mask();
  for (uint8_t r = 0; r < mp::ROWS; r++)
    for (uint8_t c = 0; c < mp::COLS; c++) u.keys_down[r][c] = g_engine.key_down(r, c);
  u.joy_x = g_joy.x();
  u.joy_y = g_joy.y();
  u.joy_mode = uint8_t(effective_joy_mode());
  u.joy_button = g_joy_sw;
  u.usb_mounted = hw::usb_mounted();
  u.usb_suspended = hw::usb_suspended();
  u.leds = hw::usb_leds();
  u.displays_on = st.displays_on;
  u.macro_running = g_engine.macro_running();
  const uint32_t idle = now - g_last_activity_ms;
  uint8_t b = st.brightness;
  if (idle > cfg::SLEEP_AFTER_MS || u.usb_suspended) b = 0;
  else if (idle > cfg::DIM_AFTER_MS && b > cfg::BRIGHTNESS_DIMMED) b = cfg::BRIGHTNESS_DIMMED;
  u.brightness = b;
  if (now - g_last_second_ms >= 1000) {
    g_last_second_ms = now;
    for (uint8_t i = 1; i < ui::ACTIVITY_SAMPLES; i++) u.activity[i - 1] = u.activity[i];
    u.activity[ui::ACTIVITY_SAMPLES - 1] = g_presses_this_second;
    g_presses_this_second = 0;
  }
  const auto& lp = g_engine.last_press();
  u.last_row = lp.row;
  u.last_col = lp.col;
  u.last_layer = lp.layer;
  ui::publish(u);
}

}  // namespace

// ==================================================================== core 0
void setup() {
  Serial.begin(115200);
  hw::settings_load();
  hw::matrix_init();
  if (hw::matrix_boot_combo_held()) rp2040.rebootToBootloader();

  pinMode(cfg::JOY_SW_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  auto& st = hw::settings();
  mp::JoyConfig jc;
  jc.invert_x = cfg::JOY_INVERT_X;
  jc.invert_y = cfg::JOY_INVERT_Y;
  jc.swap_xy = cfg::JOY_SWAP_XY;
  g_joy.set_config(jc);
  g_joy.set_cal(st.joy_cal);
  // the stick is at rest while plugging in: take the centre from here
  g_joy.calibrate_center(read_adc_avg(cfg::JOY_X_PIN), read_adc_avg(cfg::JOY_Y_PIN));

  g_engine.set_base_layer(st.base_layer < mp::NUM_LAYERS ? st.base_layer : 0);
  g_saved_base = g_engine.base_layer();
  hw::usb_begin();
  g_last_activity_ms = millis();
}

void loop() {
  const uint32_t now = millis();
  const uint32_t us = micros();
  if (us - g_last_scan_us >= cfg::SCAN_INTERVAL_US) {
    g_last_scan_us = us;
    uint8_t raw[mp::ROWS];
    hw::matrix_scan(raw);
    g_debounce.update(raw, now, [&](const mp::KeyEvent& ev) {
      g_engine.process(ev);
      activity(now);
      if (ev.pressed && g_presses_this_second < 255) g_presses_this_second++;
    });
  }
  g_engine.tick(now);
  joystick_task(now);
  hw::usb_set_keyboard(g_engine.hid());
  hw::usb_task();

  if (g_engine.base_layer() != g_saved_base) {  // remember the last base layer
    g_saved_base = g_engine.base_layer();
    hw::settings().base_layer = g_saved_base;
    hw::settings_touch(now);
  }
  hw::settings_task(now);
  publish_ui(now);
  console(now);
}

// ==================================================================== core 1
void setup1() { ui::display_begin(); }

void loop1() {
  ui::display_task(millis());
  delay(1);
}
