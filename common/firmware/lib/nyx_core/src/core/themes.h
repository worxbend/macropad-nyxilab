// Display themes: the index is saved in flash (Settings.main_theme / bar_theme, one per display);
// the colours live in nyx_ui.  Append only: a new theme goes before THEME_COUNT.
#pragma once
#include <stdint.h>

namespace mp {

enum : uint8_t { THEME_NYX, THEME_MIDNIGHT, THEME_FOREST, THEME_AMBER, THEME_MONO, THEME_LIGHT, THEME_COUNT };

inline const char* theme_name(uint8_t theme) {
  static const char* const kNames[THEME_COUNT] = {"NYX", "MIDNIGHT", "FOREST", "AMBER", "MONO", "LIGHT"};
  return kNames[theme < THEME_COUNT ? theme : uint8_t(THEME_NYX)];  // a bad flash byte reads as the default
}

}  // namespace mp
