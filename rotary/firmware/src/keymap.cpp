// ============================================================================
//  Default keymap, ROTARY edition - edit me.
//
//  Rows go from the back of the case (row 0) to the front (row 3).
//  Column 0 is the single column next to the bar display, columns 1-2 are
//  the block on the right.  Labels are shown on the 1.9" display.
//
//  The front-left key is the layer key: tap = next layer, hold = FN layer.
//  Hold the back-left + front-right keys while plugging in to enter the
//  UF2 bootloader (see BOOT_COMBO in core/keymap.h).
//
//  Each layer also says what the knob does while it is on top:
//  KNOB(clockwise, counter-clockwise, press, "LABEL").  Turning taps the
//  action once per detent; pressing holds it like a key.  CENTRE_INHERIT
//  falls through to the layer below.  FN + KNOB swaps the current layer's
//  binding for one of the presets below (saved in flash), FN + REV flips
//  the turning direction.
//
//  F1-F24 are on three pages of 8 (the right block, row by row from the back): the left
//  column is LAYER, NEXT, PREV, FIRST from the front.  The pages are layers marked as
//  further pages of L_F1 (View::Keys, L_F1): LAYER skips them, the displays show one layer.
//
//  The HOST layer shows a dashboard from the computer instead of key labels, on
//  three pages (L_SWAY and L_PLAYER are further pages of L_HOST): SYS = CPU,
//  memory, ... (View::Host), SWAY = sway workspaces (View::Sway), PLAY = the
//  media player (View::Player).  The left column is LAYER, SYS, SWAY, PLAY from
//  the front.  It needs the daemon in host/ (nyxd) and is left out of the LAYER
//  cycle until it connects.  HOST(n) keys send event n to the daemon: 1-8 run the
//  commands its config maps to them, 201-208 switch to sway workspace 1-8,
//  211-218 control the player (a config entry for any id runs that instead).
//
//  CONFIG (FN + CFG) sets the looks: the display themes, the LED colour, level and
//  effect, and the display brightness; the main display shows each current value
//  (View::Config).  Like FN it is outside the LAYER cycle (off_cycle): BACK or LAYER
//  leaves it.
// ============================================================================
#include "presets.h"

namespace mp {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

enum LayerId : uint8_t { L_MEDIA, L_EDIT, L_F1, L_F9, L_F17, L_HOST, L_SWAY, L_PLAYER, L_CFG, L_FN };
enum TapHoldId : uint8_t { TH_LAYER };
enum MacroId : uint8_t { M_HELLO };

const TapHoldDef TAP_HOLDS[] = {
    /* TH_LAYER */ {LNEXT, MO(L_FN), 220},
};

// Macros are typed as text using the US layout.
const char* const MACROS[] = {
    /* M_HELLO */ "Hello from the Nyxilab macropad!",
};

// Bindings that are both a preset and a layer's own knob: FN + KNOB hides a preset only while the two are equal.
constexpr EncoderDef KNOB_VOLUME = {CC(cc::VOL_UP), CC(cc::VOL_DOWN), CC(cc::MUTE), "VOLUME"};
constexpr EncoderDef KNOB_SCROLL = {WHEEL(-1), WHEEL(1), MB(MB_MIDDLE), "SCROLL"};

// Knob presets: FN + KNOB cycles the base layer between its own binding and these.
// The index is saved in flash (Settings.centre_modes): append only, or bump kVersion in settings.cpp.
const EncoderDef ENC_PRESETS[] = {
    KNOB_VOLUME,
    KNOB_SCROLL,
    {KC(MOD_LCTRL, hid::EQUAL), KC(MOD_LCTRL, hid::MINUS), KC(MOD_LCTRL, hid::N0), "ZOOM"},
    {K(hid::RIGHT), K(hid::LEFT), K(hid::ENTER), "ARROWS"},
};

// Shortcuts use Ctrl; on macOS swap MOD_LCTRL for MOD_LGUI.
const Layer LAYERS[] = {
    /* L_MEDIA */ {"MEDIA", rgb565(139, 92, 246), CentreDef{JoyMode::Inherit, KNOB_VOLUME}, {
        {{CC(cc::MUTE), "MUTE"},   {CC(cc::VOL_DOWN), "VOL-"},     {CC(cc::VOL_UP), "VOL+"}},
        {{CC(cc::PREV), "PREV"},   {CC(cc::PLAY_PAUSE), "PLAY"},   {CC(cc::NEXT), "NEXT"}},
        {{CC(cc::STOP), "STOP"},   {CC(cc::BRIGHT_DOWN), "BRI-"},  {CC(cc::BRIGHT_UP), "BRI+"}},
        {{TH(TH_LAYER), "LAYER"},  {CC(cc::BROWSER_BACK), "BACK"}, {CC(cc::BROWSER_FWD), "FWD"}},
    }},
    /* L_EDIT */ {"EDIT", rgb565(20, 184, 166),
                  KNOB(KC(MOD_LCTRL | MOD_LSHIFT, hid::Z), KC(MOD_LCTRL, hid::Z), KC(MOD_LCTRL, hid::S), "HISTORY"), {
        {{KC(MOD_LCTRL, hid::Z), "UNDO"}, {KC(MOD_LCTRL | MOD_LSHIFT, hid::Z), "REDO"}, {KC(MOD_LCTRL, hid::S), "SAVE"}},
        {{KC(MOD_LCTRL, hid::X), "CUT"},  {KC(MOD_LCTRL, hid::C), "COPY"},                {KC(MOD_LCTRL, hid::V), "PASTE"}},
        {{KC(MOD_LCTRL, hid::F), "FIND"}, {K(hid::HOME), "HOME"},                          {K(hid::END), "END"}},
        {{TH(TH_LAYER), "LAYER"},         {K(hid::ENTER), "ENTER"},                        {K(hid::BSPC), "BKSP"}},
    }},
    /* L_F1 */ {"F1-8", rgb565(245, 158, 11), CentreDef{JoyMode::Inherit, KNOB_SCROLL}, {
        {{TO(L_F1), "FIRST"},      {K(hid::F1), "F1"},   {K(hid::F2), "F2"}},
        {{TO(L_F17), "PREV"},      {K(hid::F3), "F3"},   {K(hid::F4), "F4"}},
        {{TO(L_F9), "NEXT"},       {K(hid::F5), "F5"},   {K(hid::F6), "F6"}},
        {{TH(TH_LAYER), "LAYER"},  {K(hid::F7), "F7"},   {K(hid::F8), "F8"}},
    }},
    /* L_F9 */ {"F9-16", rgb565(245, 158, 11), CentreDef{JoyMode::Inherit, KNOB_SCROLL}, {
        {{TO(L_F1), "FIRST"},      {K(hid::F9), "F9"},   {K(hid::F10), "F10"}},
        {{TO(L_F1), "PREV"},       {K(hid::F11), "F11"}, {K(hid::F12), "F12"}},
        {{TO(L_F17), "NEXT"},      {K(hid::F13), "F13"}, {K(hid::F14), "F14"}},
        {{TH(TH_LAYER), "LAYER"},  {K(hid::F15), "F15"}, {K(hid::F16), "F16"}},
    }, View::Keys, L_F1},
    /* L_F17 */ {"F17-24", rgb565(245, 158, 11), CentreDef{JoyMode::Inherit, KNOB_SCROLL}, {
        {{TO(L_F1), "FIRST"},      {K(hid::F17), "F17"}, {K(hid::F18), "F18"}},
        {{TO(L_F9), "PREV"},       {K(hid::F19), "F19"}, {K(hid::F20), "F20"}},
        {{TO(L_F1), "NEXT"},       {K(hid::F21), "F21"}, {K(hid::F22), "F22"}},
        {{TH(TH_LAYER), "LAYER"},  {K(hid::F23), "F23"}, {K(hid::F24), "F24"}},
    }, View::Keys, L_F1},
    /* L_HOST */ {"HOST", rgb565(56, 189, 248), CentreDef{JoyMode::Inherit, KNOB_VOLUME}, {
        {{TO(L_PLAYER), "PLAY"},   {HOST(7), "H7"},       {HOST(8), "H8"}},
        {{TO(L_SWAY), "SWAY"},     {HOST(5), "H5"},       {HOST(6), "H6"}},
        {{TO(L_HOST), "SYS"},      {HOST(3), "H3"},       {HOST(4), "H4"}},
        {{TH(TH_LAYER), "LAYER"},  {HOST(1), "H1"},       {HOST(2), "H2"}},
    }, View::Host},
    /* L_SWAY */ {"SWAY", rgb565(56, 189, 248), CentreDef{JoyMode::Inherit, KNOB_VOLUME}, {
        {{TO(L_PLAYER), "PLAY"},   {HOST(207), "7"},      {HOST(208), "8"}},
        {{TO(L_SWAY), "SWAY"},     {HOST(205), "5"},      {HOST(206), "6"}},
        {{TO(L_HOST), "SYS"},      {HOST(203), "3"},      {HOST(204), "4"}},
        {{TH(TH_LAYER), "LAYER"},  {HOST(201), "1"},      {HOST(202), "2"}},
    }, View::Sway, L_HOST},
    /* L_PLAYER */ {"PLAYER", rgb565(56, 189, 248), CentreDef{JoyMode::Inherit, KNOB_VOLUME}, {
        {{TO(L_PLAYER), "PLAY"},   {HOST(217), "VOL-"},   {HOST(218), "VOL+"}},
        {{TO(L_SWAY), "SWAY"},     {HOST(215), "-10s"},   {HOST(216), "+10s"}},
        {{TO(L_HOST), "SYS"},      {HOST(213), "PREV"},   {HOST(214), "STOP"}},
        {{TH(TH_LAYER), "LAYER"},  {HOST(211), "PLAY"},   {HOST(212), "NEXT"}},
    }, View::Player, L_HOST},
    /* L_CFG */ {"CONFIG", rgb565(148, 163, 184), 
                KNOB(SYS(SYS_BRIGHT_UP), SYS(SYS_BRIGHT_DOWN), SYS(SYS_DISPLAYS_TOGGLE), "BRIGHT"), {
        {{TO(L_MEDIA), "BACK"},             {SYS(SYS_MAIN_THEME), "MAIN"},  {SYS(SYS_BAR_THEME), "BAR"}},
        {{SYS(SYS_THEME_RESET), "RESET"},   {SYS(SYS_LED_COLOR), "LEDCOL"}, {SYS(SYS_LED_MODE), "LEDFX"}},
        {{SYS(SYS_DISPLAYS_TOGGLE), "SCRN"}, {SYS(SYS_LED_DOWN), "LED-"},   {SYS(SYS_LED_UP), "LED+"}},
        {{TH(TH_LAYER), "LAYER"},           {SYS(SYS_BRIGHT_DOWN), "DIM-"}, {SYS(SYS_BRIGHT_UP), "DIM+"}},
    }, View::Config, NOT_A_PAGE, true},
    /* L_FN */ {"FN", rgb565(239, 68, 68),
                KNOB(SYS(SYS_BRIGHT_UP), SYS(SYS_BRIGHT_DOWN), SYS(SYS_DISPLAYS_TOGGLE), "BRIGHT"), {
        {{SYS(SYS_BOOTLOADER), "BOOT"},   {SYS(SYS_BRIGHT_DOWN), "DIM-"},   {SYS(SYS_BRIGHT_UP), "DIM+"}},
        {{KNOB_CYCLE, "KNOB"},            {SYS(SYS_ENC_REVERSE), "REV"},    {SYS(SYS_DISPLAYS_TOGGLE), "SCRN"}},
        {{TO(L_MEDIA), "MEDIA"},          {TO(L_EDIT), "EDIT"},             {TO(L_F1), "FKEYS"}},
        {{___, "(held)"},                 {TO(L_CFG), "CFG"},               {SYS(SYS_LED_MODE), "LED"}},
    }},
};

const uint8_t NUM_LAYERS = sizeof(LAYERS) / sizeof(LAYERS[0]);
const uint8_t FN_LAYER = L_FN;
const uint8_t NUM_TAP_HOLDS = sizeof(TAP_HOLDS) / sizeof(TAP_HOLDS[0]);
const uint8_t NUM_MACROS = sizeof(MACROS) / sizeof(MACROS[0]);
const uint8_t NUM_ENC_PRESETS = sizeof(ENC_PRESETS) / sizeof(ENC_PRESETS[0]);

static_assert(sizeof(LAYERS) / sizeof(LAYERS[0]) <= MAX_LAYERS, "too many layers");

}  // namespace mp
