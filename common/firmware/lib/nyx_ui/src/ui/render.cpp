// Pure drawing code for both screens (Adafruit GFX canvases only, no SPI),
// so it can also be compiled on the PC by tools/ui_sim.
#include "render.h"

#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <math.h>

#include "core/keymap.h"

namespace ui {
namespace {

// ----------------------------------------------------------------- palette
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t BG = rgb(9, 10, 15);
constexpr uint16_t PANEL = rgb(22, 24, 32);
constexpr uint16_t CELL = rgb(28, 30, 40);
constexpr uint16_t MUTED = rgb(88, 92, 108);
constexpr uint16_t TEXT = rgb(236, 236, 242);
constexpr uint16_t SUBTLE = rgb(150, 154, 170);
constexpr uint16_t GOOD = rgb(52, 211, 153);
constexpr uint16_t WARN = rgb(250, 204, 21);
constexpr uint16_t FN_RED = rgb(239, 68, 68);
constexpr int KNOB_DETENTS = 20;  // KY-040

uint16_t scale(uint16_t c, uint8_t f) {  // f/255 brightness
  uint16_t r = ((c >> 11) & 0x1F) * f / 255, g = ((c >> 5) & 0x3F) * f / 255, b = (c & 0x1F) * f / 255;
  return uint16_t((r << 11) | (g << 5) | b);
}

void text_at(GFXcanvas16& c, const char* s, int x, int baseline, const GFXfont* f, uint16_t col) {
  c.setFont(f);
  c.setTextSize(1);
  c.setTextColor(col);
  c.setCursor(x, baseline);
  c.print(s);
}

void text_centered(GFXcanvas16& c, const char* s, int cx, int cy, const GFXfont* f, uint16_t col, uint8_t size = 1) {
  c.setFont(f);
  c.setTextSize(size);
  int16_t x1, y1;
  uint16_t w, h;
  c.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  c.setTextColor(col);
  c.setCursor(cx - int(w) / 2 - x1, cy - int(h) / 2 - y1);
  c.print(s);
}

const char* joy_mode_name(uint8_t m) {
  switch (mp::JoyMode(m)) {
    case mp::JoyMode::Mouse: return "MOUSE";
    case mp::JoyMode::Scroll: return "SCROLL";
    case mp::JoyMode::Arrows: return "ARROWS";
    default: return "OFF";
  }
}

// label that the key would produce right now (mirrors Engine::resolve)
const char* key_label(const UiState& s, uint8_t r, uint8_t col, uint8_t* from_layer) {
  for (int l = mp::NUM_LAYERS - 1; l >= 0; l--) {
    if (!(s.layer_mask & (1u << l))) continue;
    const mp::KeyDef& k = mp::LAYERS[l].keys[r][col];
    if (mp::act_type(k.action) == mp::ActType::Transparent) continue;
    *from_layer = uint8_t(l);
    return mp::act_type(k.action) == mp::ActType::None ? "" : k.label;
  }
  *from_layer = s.base_layer;
  return "";
}

void joy_icon(GFXcanvas16& c, uint8_t mode, int cx, int cy, uint16_t col) {
  switch (mp::JoyMode(mode)) {
    case mp::JoyMode::Mouse:
      c.drawRoundRect(cx - 8, cy - 12, 16, 24, 8, col);
      c.drawFastVLine(cx, cy - 12, 9, col);
      c.drawFastHLine(cx - 8, cy - 3, 16, col);
      break;
    case mp::JoyMode::Scroll:
      c.fillTriangle(cx, cy - 13, cx - 6, cy - 5, cx + 6, cy - 5, col);
      c.fillTriangle(cx, cy + 13, cx - 6, cy + 5, cx + 6, cy + 5, col);
      c.drawRoundRect(cx - 3, cy - 3, 6, 6, 2, col);
      break;
    case mp::JoyMode::Arrows:
      c.fillTriangle(cx, cy - 13, cx - 5, cy - 6, cx + 5, cy - 6, col);
      c.fillTriangle(cx, cy + 13, cx - 5, cy + 6, cx + 5, cy + 6, col);
      c.fillTriangle(cx - 13, cy, cx - 6, cy - 5, cx - 6, cy + 5, col);
      c.fillTriangle(cx + 13, cy, cx + 6, cy - 5, cx + 6, cy + 5, col);
      break;
    default:
      c.drawCircle(cx, cy, 10, col);
      c.drawLine(cx - 7, cy + 7, cx + 7, cy - 7, col);
      break;
  }
}

void knob_icon(GFXcanvas16& c, int cx, int cy, uint16_t col) {
  c.drawCircle(cx, cy, 9, col);
  c.fillCircle(cx, cy - 5, 2, col);
  for (int i = -1; i <= 1; i += 2) {  // grip marks
    c.drawLine(cx + i * 11, cy - 4, cx + i * 13, cy - 6, col);
    c.drawLine(cx + i * 11, cy + 4, cx + i * 13, cy + 6, col);
  }
}

// dial with one tick per detent; the pointer follows the knob's indicator dot
void knob_widget(GFXcanvas16& c, const UiState& s, uint16_t accent, int cx, int cy, int R) {
  const int pos = int(((s.enc_pos % KNOB_DETENTS) + KNOB_DETENTS) % KNOB_DETENTS);
  for (int i = 0; i < KNOB_DETENTS; i++) {
    const float a = float(i) * 6.2831853f / KNOB_DETENTS;  // 0 = up, clockwise
    const float sx = sinf(a), sy = -cosf(a);
    const int r0 = R + 3, r1 = R + (i == pos ? 9 : 6);
    c.drawLine(cx + int(sx * r0), cy + int(sy * r0), cx + int(sx * r1), cy + int(sy * r1),
               i == pos ? accent : scale(MUTED, 170));
  }
  c.fillCircle(cx, cy, R, s.enc_press ? scale(accent, 70) : CELL);
  c.drawCircle(cx, cy, R, s.enc_press ? accent : MUTED);
  c.drawCircle(cx, cy, R - 4, scale(MUTED, 110));
  const float a = float(pos) * 6.2831853f / KNOB_DETENTS;
  c.fillCircle(cx + int(sinf(a) * (R - 11)), cy - int(cosf(a) * (R - 11)), 5, accent);
}

// ------------------------------------------------ main display 320 x 170
void render_main(GFXcanvas16& c, const UiState& s) {
  const mp::Layer& top = mp::LAYERS[s.top_layer];
  const uint16_t accent = top.color;
  c.fillScreen(BG);

  // header: layer name, layer dots, status
  c.fillRect(0, 0, 320, 26, PANEL);
  c.fillRect(0, 0, 5, 26, accent);
  text_at(c, top.name, 12, 20, &FreeSansBold12pt7b, accent);
  int dx = 150;
  for (uint8_t l = 0; l < mp::NUM_LAYERS; l++) {
    if (l == mp::FN_LAYER) continue;
    if (l == s.base_layer)
      c.fillCircle(dx, 13, 4, mp::LAYERS[l].color);
    else
      c.drawCircle(dx, 13, 4, MUTED);
    dx += 13;
  }
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(s.leds & 0x02 ? WARN : MUTED);
  c.setCursor(236, 10);
  c.print("CAPS");
  c.fillCircle(290, 13, 4, s.usb_mounted ? (s.usb_suspended ? WARN : GOOD) : FN_RED);
  c.setTextColor(SUBTLE);
  c.setCursor(298, 10);
  c.print("USB");

  // key cells mirror the physical layout: 1 column left, 2 columns right
  const int col_x[mp::COLS] = {6, 174, 246};
  for (uint8_t r = 0; r < mp::ROWS; r++) {
    for (uint8_t k = 0; k < mp::COLS; k++) {
      const int x = col_x[k], y = 31 + r * 35, w = 68, h = 31;
      uint8_t from = 0;
      const char* label = key_label(s, r, k, &from);
      const uint16_t kc = mp::LAYERS[from].color;
      if (s.keys_down[r][k]) {
        c.fillRoundRect(x, y, w, h, 7, kc);
        text_centered(c, label, x + w / 2, y + h / 2, &FreeSansBold9pt7b, BG);
      } else {
        c.fillRoundRect(x, y, w, h, 7, CELL);
        c.drawRoundRect(x, y, w, h, 7, scale(kc, from == s.base_layer ? 110 : 200));
        text_centered(c, label, x + w / 2, y + h / 2, &FreeSansBold9pt7b, *label ? TEXT : MUTED);
      }
    }
  }

  // joystick or knob widget in the centre column
  const int cx = 124, cy = 92, R = 31;
  if (s.centre_encoder) {
    knob_widget(c, s, accent, cx, cy - 2, R - 3);
    knob_icon(c, cx - 22, 148, SUBTLE);
  } else {
    c.fillCircle(cx, cy, R, CELL);
    c.drawCircle(cx, cy, R, s.joy_button ? accent : MUTED);
    c.drawFastHLine(cx - R + 4, cy, 2 * R - 8, scale(MUTED, 140));
    c.drawFastVLine(cx, cy - R + 4, 2 * R - 8, scale(MUTED, 140));
    const int px = cx + int(s.joy_x * (R - 7)), py = cy - int(s.joy_y * (R - 7));
    c.drawLine(cx, cy, px, py, scale(accent, 150));
    c.fillCircle(px, py, s.joy_button ? 7 : 6, accent);
    joy_icon(c, s.joy_mode, cx - 20, 148, SUBTLE);
  }
  c.setFont(nullptr);
  c.setTextColor(SUBTLE);
  c.setCursor(cx - 4, 145);
  c.print(s.centre_encoder ? s.enc_label : joy_mode_name(s.joy_mode));
  if (s.macro_running) {
    c.setTextColor(WARN);
    c.setCursor(cx - 12, 160);
    c.print("macro...");
  }
}

// --------------------------------------------------- bar display 76 x 284
void render_bar(GFXcanvas16& c, const UiState& s) {
  const mp::Layer& top = mp::LAYERS[s.top_layer];
  c.fillScreen(BG);

  // crescent emblem + wordmark
  c.fillCircle(38, 21, 14, top.color);
  c.fillCircle(45, 15, 12, BG);
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(SUBTLE);
  c.setCursor(17, 42);
  c.print("NYXILAB");

  // base layers as pills, FN as a banner
  int y = 56;
  for (uint8_t l = 0; l < mp::NUM_LAYERS; l++) {
    if (l == mp::FN_LAYER) continue;
    const mp::Layer& L = mp::LAYERS[l];
    if (l == s.base_layer) {
      c.fillRoundRect(4, y, 68, 26, 13, L.color);
      text_centered(c, L.name, 38, y + 13, &FreeSansBold9pt7b, BG);
    } else {
      c.drawRoundRect(4, y, 68, 26, 13, MUTED);
      text_centered(c, L.name, 38, y + 13, &FreeSansBold9pt7b, SUBTLE);
    }
    y += 32;
  }
  if (s.top_layer == mp::FN_LAYER) {
    c.fillRoundRect(4, y + 2, 68, 22, 6, FN_RED);
    text_centered(c, "FN", 38, y + 13, &FreeSansBold9pt7b, BG);
  }

  // joystick mode or knob binding (small built-in font from here on)
  const int jy = 200;
  c.drawFastHLine(8, jy - 18, 60, PANEL);
  if (s.centre_encoder)
    knob_icon(c, 17, jy, top.color);
  else
    joy_icon(c, s.joy_mode, 18, jy, top.color);
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(TEXT);
  c.setCursor(s.centre_encoder ? 33 : 34, jy - 3);
  c.print(s.centre_encoder ? s.enc_label : joy_mode_name(s.joy_mode));

  // host lock LEDs
  static const char* kLocks[3] = {"NUM", "CAP", "SCR"};
  for (int i = 0; i < 3; i++) {
    const bool on = s.leds & (1u << i);
    const int x = 4 + i * 24;
    if (on)
      c.fillRoundRect(x, 222, 21, 13, 4, WARN);
    else
      c.drawRoundRect(x, 222, 21, 13, 4, MUTED);
    c.setTextColor(on ? BG : MUTED);
    c.setCursor(x + 2, 225);
    c.print(kLocks[i]);
  }

  // key activity sparkline (presses per second)
  uint8_t peak = 4;
  for (uint8_t v : s.activity) peak = v > peak ? v : peak;
  const int gy = 280, gh = 30;
  c.drawFastHLine(2, gy, 72, PANEL);
  for (int i = 0; i < ACTIVITY_SAMPLES; i++) {
    const int h = s.activity[i] * gh / peak;
    if (h > 0) c.fillRect(2 + i * 2, gy - h, 1, h, scale(top.color, uint8_t(90 + i * 4)));
  }
  c.setTextColor(MUTED);
  c.setCursor(2, 244);
  c.print(s.usb_mounted ? (s.usb_suspended ? "host asleep" : "keys/s") : "no USB");
}

}  // namespace

void render_main_screen(GFXcanvas16& c, const UiState& s) { render_main(c, s); }
void render_bar_screen(GFXcanvas16& c, const UiState& s) { render_bar(c, s); }

}  // namespace ui
