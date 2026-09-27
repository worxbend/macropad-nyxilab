// Action encoding shared by the keymap, the engine and the HID layer.
//
// An Action is 32 bits: [type:8][argument:24].  Build them with the helper
// functions at the bottom (K, KC, CC, MO, TG, ...) - see keymap.cpp.
#pragma once
#include <stdint.h>

namespace mp {

enum class ActType : uint8_t {
  None = 0,
  Transparent,  // fall through to the next active layer
  Key,          // arg = (modifiers << 8) | HID keyboard usage
  Consumer,     // arg = 16-bit HID consumer usage (media keys)
  MouseBtn,     // arg = button mask
  LayerMo,      // momentary layer while held
  LayerTg,      // toggle layer
  LayerTo,      // set base layer
  LayerNext,    // cycle base layer (skips the FN layer)
  TapHold,      // arg = index into TAP_HOLDS
  Macro,        // arg = index into MACROS
  Sys,          // arg = SysCmd
  JoyMode,      // arg = JoyMode, or 0xFF to cycle
};

enum SysCmd : uint8_t {
  SYS_BOOTLOADER = 1,  // reboot into the UF2 bootloader (BOOTSEL)
  SYS_REBOOT,
  SYS_BRIGHT_UP,
  SYS_BRIGHT_DOWN,
  SYS_JOY_CALIBRATE,  // re-centre the joystick
  SYS_DISPLAYS_TOGGLE,
};

enum class JoyMode : uint8_t { Mouse = 0, Scroll, Arrows, Off, Count, Inherit = 0xFE, Cycle = 0xFF };

using Action = uint32_t;

constexpr Action act(ActType t, uint32_t arg = 0) { return (uint32_t(t) << 24) | (arg & 0xFFFFFFu); }
constexpr ActType act_type(Action a) { return ActType(a >> 24); }
constexpr uint32_t act_arg(Action a) { return a & 0xFFFFFFu; }

// ---------------------------------------------------------------- modifiers
enum : uint8_t {
  MOD_LCTRL = 0x01,
  MOD_LSHIFT = 0x02,
  MOD_LALT = 0x04,
  MOD_LGUI = 0x08,
  MOD_RCTRL = 0x10,
  MOD_RSHIFT = 0x20,
  MOD_RALT = 0x40,
  MOD_RGUI = 0x80,
};

// ------------------------------------------------ HID keyboard usages (page 7)
namespace hid {
enum : uint8_t {
  A = 0x04, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  N1 = 0x1E, N2, N3, N4, N5, N6, N7, N8, N9, N0,
  ENTER = 0x28, ESC, BSPC, TAB, SPACE, MINUS, EQUAL, LBRC, RBRC, BSLS,
  SCLN = 0x33, QUOT, GRV, COMM, DOT, SLSH, CAPS,
  F1 = 0x3A, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  PSCR = 0x46, SCRL, PAUS, INS, HOME, PGUP, DEL, END, PGDN, RIGHT, LEFT, DOWN, UP,
  NUM = 0x53, KP_SLSH, KP_ASTR, KP_MINS, KP_PLUS, KP_ENT, KP_1, KP_2, KP_3, KP_4, KP_5, KP_6, KP_7, KP_8,
  KP_9, KP_0, KP_DOT,
  APP = 0x65,
  F13 = 0x68, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
};
}  // namespace hid

// --------------------------------------------- HID consumer usages (page 12)
namespace cc {
enum : uint16_t {
  BRIGHT_UP = 0x006F,
  BRIGHT_DOWN = 0x0070,
  NEXT = 0x00B5,
  PREV = 0x00B6,
  STOP = 0x00B7,
  PLAY_PAUSE = 0x00CD,
  MUTE = 0x00E2,
  VOL_UP = 0x00E9,
  VOL_DOWN = 0x00EA,
  CALC = 0x0192,
  FILES = 0x0194,
  BROWSER_HOME = 0x0223,
  BROWSER_BACK = 0x0224,
  BROWSER_FWD = 0x0225,
};
}  // namespace cc

enum : uint8_t { MB_LEFT = 0x01, MB_RIGHT = 0x02, MB_MIDDLE = 0x04, MB_BACK = 0x08, MB_FWD = 0x10 };

// ------------------------------------------------------------ keymap helpers
constexpr Action XX = act(ActType::None);
constexpr Action ___ = act(ActType::Transparent);
constexpr Action K(uint8_t usage) { return act(ActType::Key, usage); }
constexpr Action KC(uint8_t mods, uint8_t usage) { return act(ActType::Key, (uint32_t(mods) << 8) | usage); }
constexpr Action CC(uint16_t usage) { return act(ActType::Consumer, usage); }
constexpr Action MB(uint8_t buttons) { return act(ActType::MouseBtn, buttons); }
constexpr Action MO(uint8_t layer) { return act(ActType::LayerMo, layer); }
constexpr Action TG(uint8_t layer) { return act(ActType::LayerTg, layer); }
constexpr Action TO(uint8_t layer) { return act(ActType::LayerTo, layer); }
constexpr Action LNEXT = act(ActType::LayerNext);
constexpr Action TH(uint8_t index) { return act(ActType::TapHold, index); }
constexpr Action MACRO(uint8_t index) { return act(ActType::Macro, index); }
constexpr Action SYS(SysCmd cmd) { return act(ActType::Sys, cmd); }
constexpr Action JOY(JoyMode m) { return act(ActType::JoyMode, uint8_t(m)); }

}  // namespace mp
