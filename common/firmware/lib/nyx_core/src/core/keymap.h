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
constexpr uint8_t MAX_LAYERS = 16;

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

// What the displays show while a layer is on top.
enum class View : uint8_t {
  Keys,  // the key labels and the centre control (default)
  // The host views: a dashboard fed by the daemon on the computer instead of key labels.
  // LayerNext skips such layers while no daemon is connected.
  Host,    // system monitor (CPU, memory, ...)
  Sway,    // sway workspaces and the focused window
  Player,  // the MPRIS media player
  Config,  // the key labels with the current setting under each (themes, LEDs, brightness)
};

inline bool is_host_view(View v) { return v == View::Host || v == View::Sway || v == View::Player; }

constexpr uint8_t NOT_A_PAGE = 0xFF;

struct Layer {
  const char* name;  // shown on both displays
  uint16_t color;    // RGB565 accent colour for the UI and the LEDs
  CentreDef centre;  // stick mode or knob binding while this layer is on top
  KeyDef keys[ROWS][COLS];
  View view = View::Keys;
  // A further page of layer `page_of`: LAYER (LayerNext) skips it, the displays list the pages as one layer,
  // and they share its saved stick mode or knob preset.  Move between pages with TO().
  uint8_t page_of = NOT_A_PAGE;
  // Outside the LAYER cycle like FN: LAYER (LayerNext) skips it and the displays do not list it.
  // Reach it with TO(); LAYER on it goes on to the next layer of the cycle.
  bool off_cycle = false;
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

// The layer a page belongs to (its first page), or the layer itself.
inline uint8_t layer_group(uint8_t layer) {
  const uint8_t first = LAYERS[layer].page_of;
  return first < NUM_LAYERS ? first : layer;
}

// Whether LAYER (LayerNext) can land on the layer and the displays list it: not FN, a further page or off_cycle.
inline bool in_layer_cycle(uint8_t layer) {
  return layer != FN_LAYER && !LAYERS[layer].off_cycle && layer_group(layer) == layer;
}

// Bit l = layer l shows a host view.
inline uint32_t host_layer_mask() {
  uint32_t mask = 0;
  for (uint8_t l = 0; l < NUM_LAYERS; l++)
    if (is_host_view(LAYERS[l].view)) mask |= 1u << l;
  return mask;
}

// Keys held at power-up to enter the UF2 bootloader (works even if the keymap is broken).
constexpr uint8_t BOOT_COMBO[2][2] = {{0, 0}, {3, 2}};

}  // namespace mp
