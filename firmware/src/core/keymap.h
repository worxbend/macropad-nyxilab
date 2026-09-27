// Keymap data structures.  The actual layers live in keymap.cpp.
//
// Physical layout (top view, back of the case = row 0):
//
//     +------+------+        +------+------+------+
//     |      | C0R0 |  1.9"  | C1R0 | C2R0 |      |
//     | 2.25"| C0R1 | screen | C1R1 | C2R1 |      |
//     |  bar | C0R2 |  stick | C1R2 | C2R2 |      |
//     |      | C0R3 |   ( )  | C1R3 | C2R3 |      |
//     +------+------+        +------+------+------+
//                 (front / user side)
#pragma once
#include <stdint.h>

#include "keycodes.h"

namespace mp {

constexpr uint8_t ROWS = 4;
constexpr uint8_t COLS = 3;
constexpr uint8_t MAX_LAYERS = 8;

struct KeyDef {
  Action action;
  const char* label;  // up to ~6 characters, shown on the main display
};

struct Layer {
  const char* name;  // shown on both displays
  uint16_t color;    // RGB565 accent colour for the UI
  JoyMode joy;       // joystick mode while this layer is on top (Inherit = keep global)
  KeyDef keys[ROWS][COLS];
};

struct TapHoldDef {
  Action tap;
  Action hold;
  uint16_t term_ms;
};

extern const Layer LAYERS[];
extern const uint8_t NUM_LAYERS;
extern const uint8_t FN_LAYER;  // not part of the LayerNext cycle
extern const TapHoldDef TAP_HOLDS[];
extern const uint8_t NUM_TAP_HOLDS;
extern const char* const MACROS[];
extern const uint8_t NUM_MACROS;

// Keys held at power-up to enter the UF2 bootloader (works even if the keymap is broken).
constexpr uint8_t BOOT_COMBO[2][2] = {{0, 0}, {3, 2}};

}  // namespace mp
