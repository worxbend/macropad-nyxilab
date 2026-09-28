#include "led_fx.h"

namespace mp {
namespace {

// cosine-shaped 0..255..0 over one period, integer only
uint8_t wave(uint32_t t, uint32_t period) {
  const uint32_t ph = (t % period) * 512 / period;  // 0..511
  const uint32_t x = ph < 256 ? ph : 511 - ph;      // triangle 0..255..0
  return uint8_t(x * x * (765 - 2 * x) / 65025);    // smoothstep: eases in and out
}

uint8_t scale8(uint8_t v, uint8_t k) { return uint8_t((uint16_t(v) * (uint16_t(k) + 1)) >> 8); }

}  // namespace

Rgb rgb565_to_rgb(uint16_t c) {
  const uint8_t r = uint8_t((c >> 11) & 0x1F), g = uint8_t((c >> 5) & 0x3F), b = uint8_t(c & 0x1F);
  return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2))};
}

Rgb hsv(uint16_t hue, uint8_t sat, uint8_t val) {
  hue %= 1536;
  const uint8_t sector = uint8_t(hue >> 8), f = uint8_t(hue & 0xFF);
  const uint8_t p = scale8(val, uint8_t(255 - sat));
  const uint8_t q = scale8(val, uint8_t(255 - scale8(sat, f)));
  const uint8_t t = scale8(val, uint8_t(255 - scale8(sat, uint8_t(255 - f))));
  switch (sector) {
    case 0: return {val, t, p};
    case 1: return {q, val, p};
    case 2: return {p, val, t};
    case 3: return {p, q, val};
    case 4: return {t, p, val};
    default: return {val, p, q};
  }
}

const char* led_mode_name(LedMode m) {
  switch (m) {
    case LedMode::Layer: return "LAYER";
    case LedMode::Breathe: return "BREATHE";
    case LedMode::Rainbow: return "RAINBOW";
    default: return "OFF";
  }
}

void led_frame(const LedInput& in, uint32_t now, Rgb* out, uint8_t n) {
  // effect intensity 0..255, then a gamma-2 curve so fades look even to the eye
  uint8_t k = 0;
  switch (in.mode) {
    case LedMode::Layer: {
      const uint32_t dt = in.since_hit_ms;
      const uint8_t pulse = dt >= LED_PULSE_MS ? 0 : uint8_t(255 - dt * 255 / LED_PULSE_MS);
      k = uint8_t(190 + (65u * pulse) / 255);
      break;
    }
    case LedMode::Breathe:
      k = uint8_t(40 + (215u * wave(now, LED_BREATHE_MS)) / 255);
      break;
    case LedMode::Rainbow:
      k = 255;
      break;
    default:
      break;
  }
  const uint8_t gain = scale8(scale8(scale8(k, k), in.level), in.max_level);
  const Rgb base = rgb565_to_rgb(in.color565);
  for (uint8_t i = 0; i < n; i++) {
    Rgb c = base;
    if (in.mode == LedMode::Rainbow)
      c = hsv(uint16_t((now % LED_RAINBOW_MS) * 1536 / LED_RAINBOW_MS + uint32_t(i) * 1536 / n), 255, 255);
    out[i] = gain ? Rgb{scale8(c.r, gain), scale8(c.g, gain), scale8(c.b, gain)} : Rgb{0, 0, 0};
  }
  // keep the whole strip inside the current budget (USB 2.0 port: 500 mA for everything)
  if (in.budget_ma) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < n; i++) sum += uint32_t(out[i].r) + out[i].g + out[i].b;
    const uint32_t limit = uint32_t(in.budget_ma) * 255 / LED_MA_PER_CHANNEL;
    if (sum > limit) {
      for (uint8_t i = 0; i < n; i++)
        out[i] = {uint8_t(out[i].r * limit / sum), uint8_t(out[i].g * limit / sum), uint8_t(out[i].b * limit / sum)};
    }
  }
}

uint32_t led_current_ma(const Rgb* px, uint8_t n) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) sum += uint32_t(px[i].r) + px[i].g + px[i].b;
  return sum * LED_MA_PER_CHANNEL / 255;
}

}  // namespace mp
