// Persistent settings in flash (EEPROM emulation), written lazily to limit wear.
#pragma once
#include <stdint.h>

#include "core/joystick.h"
#include "core/keymap.h"

namespace hw {

struct Settings {
  uint32_t magic;
  uint16_t version;
  uint8_t brightness;
  uint8_t joy_modes[mp::MAX_LAYERS];  // per base layer; 0xFE = use the keymap default
  uint8_t base_layer;
  uint8_t displays_on;
  mp::JoyCal joy_cal;
};

Settings& settings();
void settings_load();
void settings_touch(uint32_t now);  // mark dirty; saved ~3 s after the last change
void settings_task(uint32_t now);

}  // namespace hw
