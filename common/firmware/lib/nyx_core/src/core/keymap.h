// Keymap data structures.  The actual layers live in <edition>/firmware/src/keymap.cpp.
//
// Physical layout (top view, back of the case = row 0):
//
//     +------+------+        +------+------+------+
//     |      | C0R0 |  1.9"  | C1R0 | C2R0 |      |
//     | 2.25"| C0R1 | screen | C1R1 | C2R1 |      |
//     |  bar | C0R2 | stick  | C1R2 | C2R2 |      |
//     |      | C0R3 | or knob| C1R3 | C2R3 |      |
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

// Rotary edition: what the knob does.  cw / ccw fire once per detent (keys are
// tapped, wheel steps and system commands run immediately); press is held like a key.
struct EncoderDef {
  Action cw = ___;
  Action ccw = ___;
  Action press = ___;
  const char* label = "";  // shown on the displays, ~7 characters
};

inline bool same_binding(const EncoderDef& a, const EncoderDef& b) {
  return a.cw == b.cw && a.ccw == b.ccw && a.press == b.press;
}

// What the centre control does while a layer is on top.  Build it with STICK()
// in the joystick edition or KNOB() in the rotary edition; CENTRE_INHERIT falls
// through to the layer below (or the saved choice for that base layer).
struct CentreDef {
  JoyMode joy = JoyMode::Inherit;
  EncoderDef knob = {};
};

constexpr CentreDef STICK(JoyMode mode) { return {mode, {}}; }
constexpr CentreDef KNOB(Action cw, Action ccw, Action press, const char* label) {
  return {JoyMode::Inherit, {cw, ccw, press, label}};
}
constexpr CentreDef CENTRE_INHERIT = {};

struct Layer {
  const char* name;  // shown on both displays
  uint16_t color;    // RGB565 accent colour for the UI and the LEDs
  CentreDef centre;  // stick mode or knob binding while this layer is on top
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
