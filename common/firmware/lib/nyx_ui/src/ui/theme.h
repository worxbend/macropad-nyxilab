// Display themes: the neutral colours of a screen (Settings.main_theme / bar_theme pick one per display).
// The accent stays the layer colour and the status colours stay; on a light theme ink() darkens them where
// they are drawn as text or thin lines, so every page stays readable.  Pure data: the host tests include it.
#pragma once
#include <stdint.h>

#include "core/themes.h"

namespace ui {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline uint16_t scale(uint16_t c, uint8_t f) {  // f/255 brightness
  uint16_t r = ((c >> 11) & 0x1F) * f / 255, g = ((c >> 5) & 0x3F) * f / 255, b = (c & 0x1F) * f / 255;
  return uint16_t((r << 11) | (g << 5) | b);
}

constexpr uint16_t GOOD = rgb(52, 211, 153);
constexpr uint16_t WARN = rgb(250, 204, 21);
constexpr uint16_t FN_RED = rgb(239, 68, 68);

struct Theme {
  uint16_t bg, panel, cell, muted, text, subtle;
  bool light;  // dark text on a light background
};

// Indexed by mp::THEME_*; a bad index (a bad flash byte) gives NYX.
inline const Theme& theme(uint8_t index) {
  static const Theme kThemes[mp::THEME_COUNT] = {
      /* NYX */ {rgb(9, 10, 15), rgb(22, 24, 32), rgb(28, 30, 40), rgb(88, 92, 108), rgb(236, 236, 242),
                 rgb(150, 154, 170), false},
      /* MIDNIGHT */ {rgb(5, 9, 28), rgb(14, 22, 52), rgb(20, 30, 66), rgb(84, 102, 150), rgb(226, 232, 255),
                      rgb(140, 156, 204), false},
      /* FOREST */ {rgb(6, 16, 10), rgb(14, 32, 20), rgb(20, 42, 28), rgb(80, 116, 90), rgb(226, 242, 228),
                    rgb(140, 178, 150), false},
      /* AMBER */ {rgb(20, 12, 6), rgb(38, 24, 14), rgb(50, 32, 18), rgb(124, 96, 68), rgb(250, 236, 214),
                   rgb(198, 168, 132), false},
      /* MONO */ {rgb(0, 0, 0), rgb(24, 24, 24), rgb(34, 34, 34), rgb(100, 100, 100), rgb(255, 255, 255),
                  rgb(164, 164, 164), false},
      /* LIGHT */ {rgb(234, 236, 241), rgb(212, 216, 225), rgb(250, 251, 253), rgb(116, 122, 136), rgb(18, 20, 28),
                   rgb(72, 78, 92), true},
  };
  return kThemes[index < mp::THEME_COUNT ? index : uint8_t(mp::THEME_NYX)];
}

// An accent or status colour drawn as text or a thin line on the theme's background: darker on a light theme.
inline uint16_t ink(const Theme& th, uint16_t c) { return th.light ? scale(c, 140) : c; }

// Text on an accent or status fill (a pressed key, the active pill, a lit lock LED).
inline uint16_t on_fill(const Theme& th) { return th.light ? th.text : th.bg; }

}  // namespace ui
