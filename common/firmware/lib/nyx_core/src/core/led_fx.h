// Effects for the RGB LED sticks on the base (2 x 8 WS2812B, chained).
// Pure C++ (no Arduino): the UI core calls led_frame() ~50 times a second and
// pushes the result to the LEDs; test/test_core and tools/ui_sim use it too.
#pragma once
#include <stdint.h>

namespace mp {

enum class LedMode : uint8_t {
  Layer = 0,  // accent colour of the active layer, flashes brighter on every key press
  Breathe,    // accent colour, slow breathing
  Rainbow,    // colour wheel running around the case
  Off,
  Count,
};

struct Rgb {
  uint8_t r, g, b;
};

struct LedInput {
  LedMode mode = LedMode::Layer;
  uint16_t color565 = 0;                 // accent colour of the top layer (RGB565)
  uint8_t level = 255;                   // overall brightness, follows the display backlight
  uint8_t max_level = 255;               // brightness cap
  uint16_t budget_ma = 0;                // LED current limit (0 = none): frames are scaled down to fit
  uint32_t since_hit_ms = 0xFFFFFFFFu;  // time since the last key press or knob detent
};

// Current of one colour channel at 255 (WS2812B at 5 V: 12..16 mA; less on 4.3 V).
constexpr uint8_t LED_MA_PER_CHANNEL = 14;
uint32_t led_current_ma(const Rgb* px, uint8_t n);  // estimate for a frame

constexpr uint16_t LED_PULSE_MS = 400;     // key-press flash decay (Layer mode)
constexpr uint16_t LED_BREATHE_MS = 4000;  // breathing period
constexpr uint16_t LED_RAINBOW_MS = 8000;  // one turn of the colour wheel

// Fill out[0..n) for time `now` (ms).
void led_frame(const LedInput& in, uint32_t now, Rgb* out, uint8_t n);

Rgb rgb565_to_rgb(uint16_t c);
Rgb hsv(uint16_t hue, uint8_t sat, uint8_t val);  // hue 0..1535 (6 x 256)
const char* led_mode_name(LedMode m);

}  // namespace mp
