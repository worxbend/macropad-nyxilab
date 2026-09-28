#include "leds.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "config.h"
#include "core/keymap.h"
#include "core/led_fx.h"

namespace ui {
namespace {

Adafruit_NeoPixel g_strip(cfg::LED_COUNT, cfg::LED_PIN, NEO_GRB + NEO_KHZ800);
uint32_t g_last_frame = 0;
bool g_dark = false;

}  // namespace

void leds_begin() {
  if (!cfg::LED_COUNT) return;
  g_strip.begin();  // PIO state machine, ~0.5 ms per frame for 16 LEDs
  g_strip.clear();
  g_strip.show();
  g_dark = true;
}

void leds_task(uint32_t now, const UiState& s) {
  if (!cfg::LED_COUNT || now - g_last_frame < 20) return;  // 50 fps
  g_last_frame = now;
  mp::LedInput in;
  in.mode = mp::LedMode(s.led_mode < uint8_t(mp::LedMode::Count) ? s.led_mode : 0);
  in.color565 = mp::LAYERS[s.top_layer].color;
  in.level = s.brightness;  // follows the backlight: dims when idle, off while the host sleeps
  in.max_level = cfg::LED_MAX_BRIGHTNESS;
  in.budget_ma = cfg::LED_BUDGET_MA;
  if (s.hit_seen) in.since_hit_ms = now - s.last_hit_ms;
  mp::Rgb px[cfg::LED_COUNT ? cfg::LED_COUNT : 1];
  mp::led_frame(in, now, px, cfg::LED_COUNT);
  bool dark = true;
  for (uint8_t i = 0; i < cfg::LED_COUNT; i++) {
    g_strip.setPixelColor(i, px[i].r, px[i].g, px[i].b);
    if (px[i].r | px[i].g | px[i].b) dark = false;
  }
  if (dark && g_dark) return;  // already off: leave the data line quiet
  g_dark = dark;
  g_strip.show();
}

}  // namespace ui
