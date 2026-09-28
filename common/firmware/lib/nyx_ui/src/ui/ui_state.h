// Snapshot of everything the displays show.  Written by core 0 (input/USB),
// read by core 1 (rendering) under a mutex.
#pragma once
#include <stdint.h>

#include "core/keymap.h"

namespace ui {

constexpr uint8_t ACTIVITY_SAMPLES = 36;  // bar display sparkline, one sample per second

struct UiState {
  uint8_t base_layer = 0;
  uint8_t top_layer = 0;
  uint32_t layer_mask = 1;
  bool keys_down[mp::ROWS][mp::COLS] = {};
  float joy_x = 0, joy_y = 0;  // -1..1 after dead zone/curve (y up)
  uint8_t joy_mode = 0;        // effective mp::JoyMode
  bool joy_button = false;
  bool usb_mounted = false;
  bool usb_suspended = false;
  uint8_t leds = 0;          // bit0 NUM, bit1 CAPS, bit2 SCROLL
  uint8_t brightness = 200;  // effective backlight (after idle dimming)
  bool displays_on = true;
  uint8_t last_row = 0xFF, last_col = 0xFF, last_layer = 0;
  uint8_t activity[ACTIVITY_SAMPLES] = {};  // key presses per second, oldest first
  uint32_t presses_total = 0;
  bool macro_running = false;

  // rotary edition: the knob replaces the stick widget
  bool centre_encoder = false;
  int32_t enc_pos = 0;          // detents since boot (dial pointer)
  bool enc_press = false;
  const char* enc_label = "";   // what the knob does right now (static string from the keymap)

  // RGB LED sticks
  uint8_t led_mode = 0;         // mp::LedMode
  uint32_t last_hit_ms = 0;     // last key press or knob detent (Layer mode flash)
  bool hit_seen = false;        // false until the first press, so boot does not flash
};

void publish(const UiState& s);  // core 0
bool fetch(UiState& out);        // core 1: returns true if something changed since the last fetch

}  // namespace ui
