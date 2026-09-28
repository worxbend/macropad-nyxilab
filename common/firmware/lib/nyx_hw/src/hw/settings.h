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
  // per base layer, 0xFE = the keymap default: stick mode (joystick edition) or knob preset (rotary edition)
  uint8_t centre_modes[mp::MAX_LAYERS];
  uint8_t base_layer;
  uint8_t displays_on;
  uint8_t led_mode;  // mp::LedMode
  // owned by the edition's centre control (joystick calibration / knob direction); zero on first boot
  uint8_t centre[CENTRE_BYTES];
};

Settings& settings();
void settings_load();
void settings_touch(uint32_t now);  // mark dirty; saved ~3 s after the last change
void settings_task(uint32_t now);

}  // namespace hw
