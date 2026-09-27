#include "settings.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "config.h"

namespace hw {
namespace {
constexpr uint32_t kMagic = 0x4E59584Cu;  // "NYXL"
constexpr uint16_t kVersion = 3;
Settings g_settings;
bool g_dirty = false;
uint32_t g_dirty_since = 0;

void defaults() {
  g_settings = Settings{};
  g_settings.magic = kMagic;
  g_settings.version = kVersion;
  g_settings.brightness = cfg::BRIGHTNESS_DEFAULT;
  for (auto& m : g_settings.joy_modes) m = uint8_t(mp::JoyMode::Inherit);
  g_settings.base_layer = 0;
  g_settings.displays_on = 1;
  g_settings.joy_cal = mp::JoyCal{};
}
}  // namespace

Settings& settings() { return g_settings; }

void settings_load() {
  EEPROM.begin(256);
  EEPROM.get(0, g_settings);
  if (g_settings.magic != kMagic || g_settings.version != kVersion) {
    defaults();
    EEPROM.put(0, g_settings);
    EEPROM.commit();
  }
}

void settings_touch(uint32_t now) {
  g_dirty = true;
  g_dirty_since = now;
}

void settings_task(uint32_t now) {
  if (g_dirty && now - g_dirty_since > 3000) {
    EEPROM.put(0, g_settings);
    EEPROM.commit();
    g_dirty = false;
  }
}

}  // namespace hw
