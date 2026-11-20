// Persistent settings in flash (EEPROM emulation), written lazily to limit wear.
#pragma once
#include <stdint.h>

#include "core/keymap.h"

namespace hw {

constexpr uint8_t CENTRE_BYTES = 16;

struct Settings {
  uint32_t magic;
  uint16_t version;
  uint8_t brightness;
  uint8_t edition;  // cfg::SETTINGS_EDITION; centre settings cannot cross editions
  // per base layer, 0xFE = the keymap default: stick mode (joystick edition) or knob preset (rotary edition)
  uint8_t centre_modes[mp::MAX_LAYERS];
  uint8_t base_layer;
  uint8_t displays_on;
  uint8_t led_mode;  // mp::LedMode
  // the looks (CONFIG layer); read them through the saved_*() functions below
  uint8_t main_theme;  // mp::THEME_*, main display
  uint8_t bar_theme;   // mp::THEME_*, bar display
  uint8_t led_color;   // mp::led_color565() index
  uint8_t led_level;   // 1..mp::LED_LEVELS
  // owned by the edition's centre control (joystick calibration / knob direction); zero on first boot
  uint8_t centre[CENTRE_BYTES];
};

Settings& settings();
void settings_load();
void settings_touch(uint32_t now);  // mark dirty; saved ~3 s after the last change
void settings_task(uint32_t now);
void settings_flush();  // write now if dirty (before a reboot)

// The looks as used: an index past its table or a level outside 1..LED_LEVELS reads as the default,
// so a bad flash byte never indexes past a table.
uint8_t saved_main_theme(const Settings& st);
uint8_t saved_bar_theme(const Settings& st);
uint8_t saved_led_color(const Settings& st);
uint8_t saved_led_level(const Settings& st);

// Runs a command that only changes the looks (SYS_MAIN_THEME ... SYS_THEME_RESET) on `st`; false for any other.
// The caller saves the change (settings_touch).
bool settings_apply(Settings& st, mp::SysCmd cmd);

}  // namespace hw
