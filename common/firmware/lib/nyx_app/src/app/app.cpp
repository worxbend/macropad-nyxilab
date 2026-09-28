#include "app.h"

#include <Arduino.h>

#include "config.h"
#include "core/debounce.h"
#include "core/keymap.h"
#include "core/led_fx.h"
#include "hw/matrix.h"
#include "hw/settings.h"
#include "hw/usb_hid.h"
#include "ui/display.h"

namespace app {
namespace {

class Host : public mp::EngineHost {
 public:
  void on_sys(mp::SysCmd cmd) override;
  void on_centre(uint8_t mode) override;
};

Host g_host;
mp::Debouncer g_debounce(cfg::DEBOUNCE_MS);
mp::Engine g_engine(g_host);
Centre* g_centre = nullptr;

uint32_t g_last_scan_us = 0, g_last_activity_ms = 0, g_last_second_ms = 0;
uint8_t g_presses_this_second = 0;
ui::UiState g_ui;
uint8_t g_saved_base = 0xFF;
uint32_t g_last_hit_ms = 0;
bool g_hit_seen = false;

void Host::on_sys(mp::SysCmd cmd) {
  auto& st = hw::settings();
  const uint32_t now = millis();
  if (g_centre && g_centre->on_sys(cmd, now)) return;
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
    case mp::SYS_DISPLAYS_TOGGLE:
      st.displays_on = !st.displays_on;
      hw::settings_touch(now);
      break;
    case mp::SYS_LED_MODE:
      st.led_mode = uint8_t((st.led_mode + 1) % uint8_t(mp::LedMode::Count));
      hw::settings_touch(now);
      break;
    default:
      break;  // a command of the other edition's centre control
  }
}

void Host::on_centre(uint8_t mode) {
  if (g_centre) g_centre->on_mode(mode, millis());
}

// ------------------------------------------------------------ serial console
void print_help() {
  Serial.printf("commands: help | info | %s | layer <n> | boot\n", g_centre ? g_centre->help() : "");
}

void console() {
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
      Serial.printf("layer base=%u top=%u mask=0x%lx usb=%d leds=0x%02x bright=%u rgb=%s\n", g_engine.base_layer(),
                    g_engine.top_layer(), (unsigned long)g_engine.layer_mask(), hw::usb_mounted(), hw::usb_leds(),
                    hw::settings().brightness, mp::led_mode_name(mp::LedMode(hw::settings().led_mode)));
      if (g_centre) g_centre->info();
    } else if (!strncmp(line, "layer ", 6)) {
      g_engine.set_base_layer(uint8_t(atoi(line + 6)));
    } else if (!strcmp(line, "boot")) {
      g_host.on_sys(mp::SYS_BOOTLOADER);
    } else if (!(g_centre && g_centre->console(line))) {
      print_help();
    }
  }
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
  if (g_centre) g_centre->fill_ui(u);
  u.led_mode = st.led_mode;
  u.last_hit_ms = g_last_hit_ms;
  u.hit_seen = g_hit_seen;
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

// ---------------------------------------------------------------- services
mp::Engine& engine() { return g_engine; }

void activity(uint32_t now) { g_last_activity_ms = now; }

void hit(uint32_t now) {  // key press or knob detent: the LEDs flash in Layer mode
  g_last_hit_ms = now;
  g_hit_seen = true;
}

void settings_changed(uint32_t now) { hw::settings_touch(now); }

// ==================================================================== core 0
void begin(Centre& centre) {
  g_centre = &centre;
  Serial.begin(115200);
  hw::settings_load();
  hw::matrix_init();
  if (hw::matrix_boot_combo_held()) rp2040.rebootToBootloader();

  auto& st = hw::settings();
  centre.begin(g_engine, st.centre);
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
      if (ev.pressed) {
        hit(now);
        if (g_presses_this_second < 255) g_presses_this_second++;
      }
    });
  }
  g_engine.tick(now);
  if (g_centre) g_centre->task(now);
  int8_t wheel, pan;
  g_engine.take_wheel(wheel, pan);  // Wheel actions (knob scroll presets, or keys)
  if (wheel || pan) hw::usb_add_mouse(0, 0, wheel, pan);
  hw::usb_set_keyboard(g_engine.hid());
  hw::usb_task();

  if (g_engine.base_layer() != g_saved_base) {  // remember the last base layer
    g_saved_base = g_engine.base_layer();
    hw::settings().base_layer = g_saved_base;
    hw::settings_touch(now);
  }
  hw::settings_task(now);
  publish_ui(now);
  console();
}

// ==================================================================== core 1
void begin1() { ui::display_begin(); }

void loop1() {
  ui::display_task(millis());
  delay(1);
}

}  // namespace app
