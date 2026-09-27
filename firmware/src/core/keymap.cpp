// ============================================================================
//  Default keymap - edit me.
//
//  Rows go from the back of the case (row 0) to the front (row 3).
//  Column 0 is the single column next to the bar display, columns 1-2 are
//  the block on the right.  Labels are shown on the 1.9" display.
//
//  The front-left key is the layer key: tap = next layer, hold = FN layer.
//  Hold the back-left + front-right keys while plugging in to enter the
//  UF2 bootloader (see BOOT_COMBO in keymap.h).
// ============================================================================
#include "keymap.h"

namespace mp {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

enum LayerId : uint8_t { L_MEDIA, L_EDIT, L_FKEYS, L_FN };
enum TapHoldId : uint8_t { TH_LAYER };
enum MacroId : uint8_t { M_HELLO };

const TapHoldDef TAP_HOLDS[] = {
    /* TH_LAYER */ {LNEXT, MO(L_FN), 220},
};

// Macros are typed as text using the US layout.
const char* const MACROS[] = {
    /* M_HELLO */ "Hello from the Nyxilab macropad!",
};

// Shortcuts use Ctrl; on macOS swap MOD_LCTRL for MOD_LGUI.
const Layer LAYERS[] = {
    /* L_MEDIA */ {"MEDIA", rgb565(139, 92, 246), JoyMode::Mouse, {
        {{CC(cc::MUTE), "MUTE"},   {CC(cc::VOL_DOWN), "VOL-"},     {CC(cc::VOL_UP), "VOL+"}},
        {{CC(cc::PREV), "PREV"},   {CC(cc::PLAY_PAUSE), "PLAY"},   {CC(cc::NEXT), "NEXT"}},
        {{CC(cc::STOP), "STOP"},   {CC(cc::BRIGHT_DOWN), "BRI-"},  {CC(cc::BRIGHT_UP), "BRI+"}},
        {{TH(TH_LAYER), "LAYER"},  {CC(cc::BROWSER_BACK), "BACK"}, {CC(cc::BROWSER_FWD), "FWD"}},
    }},
    /* L_EDIT */ {"EDIT", rgb565(20, 184, 166), JoyMode::Arrows, {
        {{KC(MOD_LCTRL, hid::Z), "UNDO"}, {KC(MOD_LCTRL | MOD_LSHIFT, hid::Z), "REDO"}, {KC(MOD_LCTRL, hid::S), "SAVE"}},
        {{KC(MOD_LCTRL, hid::X), "CUT"},  {KC(MOD_LCTRL, hid::C), "COPY"},                {KC(MOD_LCTRL, hid::V), "PASTE"}},
        {{KC(MOD_LCTRL, hid::F), "FIND"}, {K(hid::HOME), "HOME"},                          {K(hid::END), "END"}},
        {{TH(TH_LAYER), "LAYER"},         {K(hid::ENTER), "ENTER"},                        {K(hid::BSPC), "BKSP"}},
    }},
    /* L_FKEYS */ {"F13-24", rgb565(245, 158, 11), JoyMode::Scroll, {
        {{K(hid::F13), "F13"},    {K(hid::F14), "F14"}, {K(hid::F15), "F15"}},
        {{K(hid::F16), "F16"},    {K(hid::F17), "F17"}, {K(hid::F18), "F18"}},
        {{K(hid::F19), "F19"},    {K(hid::F20), "F20"}, {K(hid::F21), "F21"}},
        {{TH(TH_LAYER), "LAYER"}, {K(hid::F22), "F22"}, {K(hid::F23), "F23"}},
    }},
    /* L_FN */ {"FN", rgb565(239, 68, 68), JoyMode::Inherit, {
        {{SYS(SYS_BOOTLOADER), "BOOT"},        {SYS(SYS_BRIGHT_DOWN), "DIM-"},   {SYS(SYS_BRIGHT_UP), "DIM+"}},
        {{JOY(JoyMode::Cycle), "JOY"},         {SYS(SYS_JOY_CALIBRATE), "CAL"},  {SYS(SYS_DISPLAYS_TOGGLE), "SCRN"}},
        {{TO(L_MEDIA), "MEDIA"},               {TO(L_EDIT), "EDIT"},             {TO(L_FKEYS), "FKEYS"}},
        {{___, "(held)"},                      {MACRO(M_HELLO), "HELLO"},        {K(hid::CAPS), "CAPS"}},
    }},
};

const uint8_t NUM_LAYERS = sizeof(LAYERS) / sizeof(LAYERS[0]);
const uint8_t FN_LAYER = L_FN;
const uint8_t NUM_TAP_HOLDS = sizeof(TAP_HOLDS) / sizeof(TAP_HOLDS[0]);
const uint8_t NUM_MACROS = sizeof(MACROS) / sizeof(MACROS[0]);

static_assert(sizeof(LAYERS) / sizeof(LAYERS[0]) <= MAX_LAYERS, "too many layers");

}  // namespace mp
