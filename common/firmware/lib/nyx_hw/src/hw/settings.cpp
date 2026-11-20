#include "settings.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "config.h"
#include "core/led_fx.h"
#include "core/themes.h"

namespace hw {
namespace {
constexpr uint32_t kMagic = 0x4E59584Cu;  // "NYXL"
constexpr uint16_t kVersion = 8;
Settings g_settings;
bool g_dirty = false;
uint32_t g_dirty_since = 0;

void reset_looks(Settings& st) {
  st.main_theme = mp::THEME_NYX;
  st.bar_theme = mp::THEME_NYX;
  st.led_color = 0;  // the layer colour
  st.led_level = mp::LED_LEVELS;
}

uint8_t valid_or(uint8_t value, uint8_t count, uint8_t fallback) { return value < count ? value : fallback; }

void defaults() {
  g_settings = Settings{};
  g_settings.magic = kMagic;
  g_settings.version = kVersion;
  g_settings.brightness = cfg::BRIGHTNESS_DEFAULT;
  g_settings.edition = cfg::SETTINGS_EDITION;
  for (auto& m : g_settings.centre_modes) m = uint8_t(mp::JoyMode::Inherit);  // == ENC_LAYER_DEFAULT
  g_settings.base_layer = 0;
  g_settings.displays_on = 1;
  g_settings.led_mode = 0;
  reset_looks(g_settings);
}
}  // namespace

Settings& settings() { return g_settings; }

void settings_load() {
  EEPROM.begin(256);
  EEPROM.get(0, g_settings);
  g_dirty = false;
  bool changed = false;
  if (g_settings.magic != kMagic || g_settings.version != kVersion) {
    defaults();
    changed = true;
  } else if (g_settings.edition != cfg::SETTINGS_EDITION) {
    g_settings.edition = cfg::SETTINGS_EDITION;
    for (auto& m : g_settings.centre_modes) m = uint8_t(mp::JoyMode::Inherit);
    for (auto& b : g_settings.centre) b = 0;
    changed = true;
  }
  if (changed) {
    EEPROM.put(0, g_settings);
    EEPROM.commit();
  }
}

void settings_touch(uint32_t now) {
  g_dirty = true;
  g_dirty_since = now;
}

void settings_task(uint32_t now) {
  // signed: a stamp taken inside this loop pass can be newer than the pass's `now`
  if (g_dirty && int32_t(now - g_dirty_since) > 3000) settings_flush();
}

void settings_flush() {
  if (!g_dirty) return;
  EEPROM.put(0, g_settings);
  EEPROM.commit();
  g_dirty = false;
}

uint8_t saved_main_theme(const Settings& st) { return valid_or(st.main_theme, mp::THEME_COUNT, mp::THEME_NYX); }
uint8_t saved_bar_theme(const Settings& st) { return valid_or(st.bar_theme, mp::THEME_COUNT, mp::THEME_NYX); }
uint8_t saved_led_color(const Settings& st) { return valid_or(st.led_color, mp::LED_COLOR_COUNT, 0); }
uint8_t saved_led_level(const Settings& st) {
  return st.led_level >= 1 && st.led_level <= mp::LED_LEVELS ? st.led_level : mp::LED_LEVELS;
}

bool settings_apply(Settings& st, mp::SysCmd cmd) {
  switch (cmd) {
    case mp::SYS_MAIN_THEME:
      st.main_theme = uint8_t((saved_main_theme(st) + 1) % mp::THEME_COUNT);
      return true;
    case mp::SYS_BAR_THEME:
      st.bar_theme = uint8_t((saved_bar_theme(st) + 1) % mp::THEME_COUNT);
      return true;
    case mp::SYS_LED_COLOR:
      st.led_color = uint8_t((saved_led_color(st) + 1) % mp::LED_COLOR_COUNT);
      return true;
    case mp::SYS_LED_UP: {
      const uint8_t level = saved_led_level(st);
      st.led_level = level < mp::LED_LEVELS ? uint8_t(level + 1) : level;
      return true;
    }
    case mp::SYS_LED_DOWN: {
      const uint8_t level = saved_led_level(st);
      st.led_level = level > 1 ? uint8_t(level - 1) : level;
      return true;
    }
    case mp::SYS_THEME_RESET:
      reset_looks(st);
      return true;
    default:
      return false;
  }
}

}  // namespace hw
