// Pure drawing code for both screens (Adafruit GFX canvases only, no SPI),
// so it can also be compiled on the PC by tools/ui_sim.
#include "render.h"

#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/keymap.h"
#include "core/led_fx.h"
#include "core/themes.h"
#include "host_fmt.h"
#include "theme.h"

namespace ui {
namespace {

// ----------------------------------------------------------------- palette
// The theme of the screen being drawn.  Set by the two entry points; the renderers run on one core, one screen at
// a time, and a global spares every helper a theme parameter.
const Theme* th = &theme(mp::THEME_NYX);
constexpr int KNOB_DETENTS = 20;  // KY-040

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

// the key a press would produce right now (mirrors Engine::resolve), nullptr if none; *from_layer = its layer
const mp::KeyDef* resolved_key(const UiState& s, uint8_t r, uint8_t col, uint8_t* from_layer) {
  for (int l = mp::NUM_LAYERS - 1; l >= 0; l--) {
    if (!(s.layer_mask & (1u << l))) continue;
    const mp::KeyDef& k = mp::LAYERS[l].keys[r][col];
    if (mp::act_type(k.action) == mp::ActType::Transparent) continue;
    *from_layer = uint8_t(l);
    return mp::act_type(k.action) == mp::ActType::None ? nullptr : &k;
  }
  *from_layer = s.base_layer;
  return nullptr;
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
               i == pos ? ink(*th, accent) : scale(th->muted, 170));
  }
  c.fillCircle(cx, cy, R, s.enc_press ? scale(accent, 70) : th->cell);
  c.drawCircle(cx, cy, R, s.enc_press ? accent : th->muted);
  c.drawCircle(cx, cy, R - 4, scale(th->muted, 110));
  const float a = float(pos) * 6.2831853f / KNOB_DETENTS;
  c.fillCircle(cx + int(sinf(a) * (R - 11)), cy - int(cosf(a) * (R - 11)), 5, accent);
}


// ------------------------------------------------------ host dashboard (the host views)
uint16_t level_colour(uint8_t pct) { return pct < 60 ? GOOD : (pct < 85 ? WARN : FN_RED); }

void small(GFXcanvas16& c, const char* s, int x, int y, uint16_t col) {
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(col);
  c.setCursor(x, y);
  c.print(s);
}

void small_right(GFXcanvas16& c, const char* s, int right, int y, uint16_t col) {
  small(c, s, right - 6 * int(strlen(s)), y, col);
}

void hbar(GFXcanvas16& c, int x, int y, int w, int h, uint8_t pct) {
  c.fillRoundRect(x, y, w, h, h / 2, th->cell);
  if (pct > 100) return;
  const int f = w * pct / 100;
  if (f > 0) c.fillRoundRect(x, y, f < h ? h : f, h, h / 2, level_colour(pct));
}

void sparkline(GFXcanvas16& c, const uint8_t* v, int x, int bottom, int h, uint16_t col) {
  c.drawFastHLine(x, bottom, 2 * mp::HOST_HISTORY, th->panel);
  for (int i = 0; i < mp::HOST_HISTORY; i++) {
    const int bh = (v[i] > 100 ? 100 : v[i]) * h / 100;
    if (bh > 0) c.fillRect(x + i * 2, bottom - bh, 1, bh, scale(col, uint8_t(90 + i * 4)));
  }
}

// Width from the cursor to the right edge of `s` in font `f` (nullptr = the built-in 6 x 8 font).
int text_width(GFXcanvas16& c, const char* s, const GFXfont* f) {
  c.setFont(f);
  c.setTextSize(1);
  c.setTextWrap(false);  // measure one line, however long
  int16_t x1, y1;
  uint16_t w, h;
  c.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return x1 + int(w);
}

// A key or layer label centred on (cx, cy): bold 9 pt, or the small built-in font when that is wider than max_w.
void label_centered(GFXcanvas16& c, const char* s, int cx, int cy, int max_w, uint16_t col) {
  const GFXfont* f = text_width(c, s, &FreeSansBold9pt7b) <= max_w ? &FreeSansBold9pt7b : nullptr;
  text_centered(c, s, cx, cy, f, col);
}

// `s` in font `f`, cut with an ellipsis to `max_w` px
void text_fit(GFXcanvas16& c, const char* s, int x, int baseline, int max_w, const GFXfont* f, uint16_t col) {
  char buf[mp::HOST_MEDIA_LEN + 4];
  fit_width(buf, sizeof buf, s, max_w, [&](const char* t) { return text_width(c, t, f); });
  text_at(c, buf, x, baseline, f, col);
}

// Every HOST page: layer name, one dot per page, host name, host clock, link dot.
void render_host_header(GFXcanvas16& c, const UiState& s) {
  const mp::Layer& top = mp::LAYERS[s.top_layer];
  const mp::HostData& d = s.host;
  c.fillRect(0, 0, 320, 26, th->panel);
  c.fillRect(0, 0, 5, 26, top.color);
  text_at(c, top.name, 12, 20, &FreeSansBold12pt7b, ink(*th, top.color));
  int x = 12 + text_width(c, top.name, &FreeSansBold12pt7b) + 10;
  const HostPage page = host_page(s.top_layer);
  for (uint8_t i = 0; page.count > 1 && i < page.count; i++, x += 10) {
    if (i == page.index)
      c.fillCircle(x, 13, 3, top.color);
    else
      c.drawCircle(x, 13, 3, th->muted);
  }
  if (d.linked) {
    char host[HEADER_HOST_CHARS + 1];
    const int left = x + 2, clock = 232;  // the full name would run into the clock
    fmt_header_host(host, d.hostname, (clock - left) / 6);
    small(c, host, left, 10, th->subtle);
  }
  if (d.has_metrics && d.m.clock_h != mp::HOST_UNKNOWN) {
    char buf[8];
    snprintf(buf, sizeof buf, "%02u:%02u", d.m.clock_h, d.m.clock_m);
    text_at(c, buf, 238, 19, &FreeSansBold9pt7b, th->text);
  }
  c.fillCircle(304, 13, 5, d.linked ? GOOD : FN_RED);
}

void render_host_waiting(GFXcanvas16& c, const UiState& s) {
  const bool linked = s.host.linked;
  text_centered(c, linked ? "connected" : "waiting for host", 160, 66, &FreeSansBold12pt7b, th->text);
  if (linked) {
    small(c, "waiting for the first metrics...", 64, 96, th->subtle);
    return;
  }
  small(c, "start the bridge on your computer:", 58, 92, th->subtle);
  small(c, "systemctl --user start nyxd", 79, 108, ink(*th, WARN));
  const char* hint = s.centre_encoder ? "keys and knob keep working; FN = settings"
                                      : "keys and stick keep working; FN = settings";
  small(c, hint, (320 - 6 * int(strlen(hint))) / 2, 140, th->muted);
}

// ------------------------------------------------------ SWAY page (View::Sway)
// One cell per workspace, laid out like the keys that switch to them: 1 2 at the front (bottom), 7 8 at the back.
void workspace_grid(GFXcanvas16& c, const mp::HostSway& w, uint16_t accent) {
  constexpr int W = 40, H = 30, GAP = 4, X = 230, Y = 32;
  for (uint8_t n = 1; n <= mp::HOST_WORKSPACES; n++) {
    const int x = X + (n - 1) % 2 * (W + GAP), y = Y + (3 - (n - 1) / 2) * (H + GAP);
    const uint8_t bit = uint8_t(1u << (n - 1));
    uint16_t fill = th->panel, edge = th->panel, text = scale(th->muted, 160);
    if (!w.running) {
    } else if (w.focused == n) {
      fill = edge = accent;
      text = on_fill(*th);
    } else if (w.urgent & bit) {
      fill = edge = FN_RED;
      text = th->text;
    } else if (w.visible & bit) {
      fill = th->cell;
      edge = accent;
      text = th->text;
    } else if (w.open & bit) {
      fill = th->cell;
      edge = th->subtle;
      text = th->text;
    }
    c.fillRoundRect(x, y, W, H, 6, fill);
    c.drawRoundRect(x, y, W, H, 6, edge);
    const char num[2] = {char('0' + n), 0};
    text_centered(c, num, x + W / 2, y + H / 2, &FreeSansBold12pt7b, text);
  }
}

void render_sway_page(GFXcanvas16& c, const UiState& s) {
  const uint16_t accent = mp::LAYERS[s.top_layer].color;
  const mp::HostSway& w = s.host.sway;
  workspace_grid(c, w, accent);
  c.fillRoundRect(6, 31, 218, 134, 6, th->panel);
  if (!w.running) {
    text_centered(c, "sway not running", 115, 86, &FreeSansBold9pt7b, th->text);
    small(c, "start sway, or check SWAYSOCK", 28, 106, th->muted);
    return;
  }

  // focused workspace: number, layout, window count
  char buf[16];
  small(c, "WORKSPACE", 14, 37, th->subtle);
  if (w.focused)
    snprintf(buf, sizeof buf, "%u", w.focused);
  else
    strcpy(buf, "-");  // a workspace outside 1..8
  text_at(c, buf, 14, 76, &FreeSansBold18pt7b, ink(*th, accent));
  text_at(c, sway_layout_name(w.layout), 54, 60, &FreeSansBold9pt7b, th->text);
  fmt_windows(buf, sizeof buf, w.windows);
  small(c, buf, 54, 67, th->subtle);
  c.drawFastHLine(14, 86, 202, th->cell);

  // focused window: app_id, title in up to 3 lines
  if (!*w.app_id && !*w.title) {
    small(c, "no window", 14, 96, th->muted);
    return;
  }
  text_fit(c, w.app_id, 14, 106, 202, &FreeSansBold9pt7b, th->text);
  char lines[3][WRAP_MAX_COLS + 1];
  const int n = wrap_lines(w.title, 33, 3, lines);
  for (int i = 0; i < n; i++) small(c, lines[i], 14, 116 + i * 12, th->subtle);
}

// ---------------------------------------------------- PLAYER page (View::Player)
void status_icon(GFXcanvas16& c, uint8_t status, int x, int y) {
  switch (status) {
    case mp::PLAYER_PLAYING: c.fillTriangle(x, y, x, y + 10, x + 9, y + 5, GOOD); break;
    case mp::PLAYER_PAUSED:
      c.fillRect(x, y, 3, 11, WARN);
      c.fillRect(x + 6, y, 3, 11, WARN);
      break;
    default: c.fillRect(x, y + 1, 9, 9, th->muted); break;
  }
}

// a bar of `w` px filled to `num / den`, in the accent colour
void accent_bar(GFXcanvas16& c, int x, int y, int w, uint32_t num, uint32_t den, uint16_t accent) {
  c.fillRoundRect(x, y, w, 6, 3, th->cell);
  if (!den) return;
  const int f = int(uint64_t(num > den ? den : num) * uint32_t(w) / den);
  if (f > 0) c.fillRoundRect(x, y, f < 6 ? 6 : f, 6, 3, accent);
}

void render_player_page(GFXcanvas16& c, const UiState& s) {
  const uint16_t accent = mp::LAYERS[s.top_layer].color;
  const mp::HostPlayer& p = s.host.player;
  c.fillRoundRect(6, 31, 308, 134, 6, th->panel);
  if (!p.present) {
    text_centered(c, "no media player", 160, 82, &FreeSansBold12pt7b, th->text);
    const char* hint = "start a player; nyxd follows it with playerctl";
    small(c, hint, (320 - 6 * int(strlen(hint))) / 2, 108, th->muted);
    return;
  }

  static const char* const kStatus[] = {"stopped", "playing", "paused"};
  status_icon(c, p.status, 15, 36);
  small(c, p.name, 32, 38, th->subtle);
  small_right(c, kStatus[p.status <= mp::PLAYER_PAUSED ? p.status : 0], 306, 38, th->muted);

  text_fit(c, p.title, 14, 70, 292, &FreeSansBold12pt7b, th->text);
  text_fit(c, p.artist, 14, 92, 292, &FreeSansBold9pt7b, th->subtle);
  char buf[mp::HOST_MEDIA_LEN + 4];
  fit_width(buf, sizeof buf, p.album, 292, [](const char* t) { return 6 * int(strlen(t)); });
  small(c, buf, 14, 100, th->muted);

  accent_bar(c, 14, 118, 292, p.position_s, p.length_s, accent);
  fmt_progress(buf, sizeof buf, p.position_s, p.length_s);
  small(c, buf, 14, 130, th->text);

  if (p.volume <= 100) {
    small(c, "VOL", 14, 149, th->subtle);
    accent_bar(c, 40, 150, 230, p.volume, 100, accent);
    fmt_pct(buf, sizeof buf, p.volume);
    small_right(c, buf, 306, 149, th->text);
  }
}

// ------------------------------------------------- SYS page (View::Host)
void render_sys_page(GFXcanvas16& c, const UiState& s) {
  const mp::HostMetrics& m = s.host.m;
  char buf[32];

  // CPU: total, temperature, load
  c.fillRoundRect(6, 31, 144, 72, 6, th->panel);
  small(c, "CPU", 14, 37, th->subtle);
  fmt_temp(buf, sizeof buf, m.cpu_temp_c10);
  small_right(c, buf, 142, 37, m.cpu_temp_c10 >= 850 ? ink(*th, FN_RED) : th->subtle);
  fmt_pct(buf, sizeof buf, m.cpu_pct);
  text_at(c, buf, 14, 80, &FreeSansBold18pt7b, m.cpu_pct <= 100 ? th->text : th->muted);
  tenths(buf, sizeof buf, (m.load1_x100 + 5) / 10, "");
  small(c, "load", 104, 62, th->muted);
  small(c, buf, 104, 72, th->subtle);
  hbar(c, 14, 90, 128, 6, m.cpu_pct);

  // per-core bars
  c.fillRoundRect(156, 31, 158, 72, 6, th->panel);
  small(c, "CORES", 164, 37, th->subtle);
  if (m.n_cores) {
    const int span = 142, gap = m.n_cores > 8 ? 2 : 4, bw = (span - gap * (m.n_cores - 1)) / m.n_cores;
    for (uint8_t i = 0; i < m.n_cores; i++) {
      const int x = 164 + i * (bw + gap), h = (m.core_pct[i] > 100 ? 0 : m.core_pct[i]) * 50 / 100;
      c.fillRect(x, 47, bw, 50, th->cell);
      if (h) c.fillRect(x, 97 - h, bw, h, level_colour(m.core_pct[i]));
    }
  }

  // memory, swap, disk
  c.fillRoundRect(6, 108, 308, 20, 6, th->panel);
  small(c, "MEM", 14, 114, th->subtle);
  hbar(c, 40, 114, 170, 8, m.mem_pct());
  char used[12], total[12];
  tenths(used, sizeof used, uint32_t(uint64_t(m.mem_used_mib) * 10 / 1024), "");
  tenths(total, sizeof total, uint32_t(uint64_t(m.mem_total_mib) * 10 / 1024), "G");
  snprintf(buf, sizeof buf, "%s/%s", used, total);
  small_right(c, buf, 308, 114, th->text);

  c.fillRoundRect(6, 131, 152, 20, 6, th->panel);
  small(c, "SWAP", 14, 137, th->subtle);
  hbar(c, 44, 137, 72, 8, m.swap_pct);
  fmt_pct(buf, sizeof buf, m.swap_pct);
  small_right(c, buf, 152, 137, th->text);
  c.fillRoundRect(162, 131, 152, 20, 6, th->panel);
  small(c, "DISK", 170, 137, th->subtle);
  hbar(c, 200, 137, 72, 8, m.disk_pct);
  fmt_pct(buf, sizeof buf, m.disk_pct);
  small_right(c, buf, 308, 137, th->text);

  // network and uptime
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setCursor(14, 158);
  c.setTextColor(ink(*th, GOOD));
  c.write(0x19);  // down arrow in the built-in font
  fmt_rate(buf, sizeof buf, m.net_rx_bps);
  c.setTextColor(th->text);
  c.print(buf);
  c.print("/s  ");
  c.setTextColor(ink(*th, WARN));
  c.write(0x18);
  fmt_rate(buf, sizeof buf, m.net_tx_bps);
  c.setTextColor(th->text);
  c.print(buf);
  c.print("/s");
  fmt_uptime(buf, sizeof buf, m.uptime_s);
  char up[sizeof buf + 4];
  snprintf(up, sizeof up, "up %s", buf);
  small_right(c, up, 308, 158, th->subtle);
}

void render_host_main(GFXcanvas16& c, const UiState& s) {
  const mp::View view = mp::View(s.view);
  c.fillScreen(th->bg);
  render_host_header(c, s);
  if (!s.host.linked || (view == mp::View::Host && !s.host.has_metrics)) return render_host_waiting(c, s);
  switch (view) {
    case mp::View::Sway: return render_sway_page(c, s);
    case mp::View::Player: return render_player_page(c, s);
    default: return render_sys_page(c, s);
  }
}

// Bottom of the bar display on every HOST page: the page, and that the LAYER key still works.
constexpr int LEGEND_TOP = 236;
void layer_legend(GFXcanvas16& c, const UiState& s) {
  char page[16];
  fmt_host_page(page, sizeof page, s.top_layer);
  c.drawFastHLine(8, LEGEND_TOP, 60, th->panel);
  small(c, page, 2, LEGEND_TOP + 6, ink(*th, mp::LAYERS[s.top_layer].color));
  small(c, "LAYER:", 2, LEGEND_TOP + 18, th->subtle);
  small(c, "tap = next", 2, LEGEND_TOP + 28, th->text);
  small(c, "hold = FN", 2, LEGEND_TOP + 38, th->text);
}

void render_host_bar(GFXcanvas16& c, const UiState& s) {
  const mp::Layer& top = mp::LAYERS[s.top_layer];
  const mp::HostData& d = s.host;
  const mp::HostMetrics& m = d.m;
  char buf[24];
  c.fillScreen(th->bg);
  c.fillCircle(38, 21, 14, top.color);
  c.fillCircle(45, 15, 12, th->bg);
  small(c, "NYXILAB", 17, 42, th->subtle);

  if (!d.linked || !d.has_metrics) {
    small(c, d.linked ? "connected" : "no host", 4, 64, ink(*th, d.linked ? GOOD : FN_RED));
    small(c, "run nyxd on", 4, 84, th->muted);
    small(c, "the computer", 4, 96, th->muted);
    return layer_legend(c, s);
  }

  // CPU and memory history, one sample a second
  small(c, "CPU", 4, 56, th->subtle);
  fmt_pct(buf, sizeof buf, m.cpu_pct);
  small_right(c, buf, 74, 56, th->text);
  sparkline(c, d.cpu_hist, 2, 98, 30, top.color);
  small(c, "MEM", 4, 108, th->subtle);
  fmt_pct(buf, sizeof buf, m.mem_pct());
  small_right(c, buf, 74, 108, th->text);
  sparkline(c, d.mem_hist, 2, 150, 30, top.color);

  int y = 160;
  if (m.gpu_pct <= 100 || m.gpu_temp_c10 != mp::HOST_NO_TEMP) {
    small(c, "GPU", 4, y, th->subtle);
    char t[8];
    fmt_pct(buf, sizeof buf, m.gpu_pct);
    fmt_temp(t, sizeof t, m.gpu_temp_c10);
    strncat(buf, " ", sizeof buf - strlen(buf) - 1);
    strncat(buf, t, sizeof buf - strlen(buf) - 1);
    small_right(c, buf, 74, y, th->text);
    y += 14;
  }

  // text slots pushed with `nyxctl text <slot> ...`: two lines of 12 characters each
  c.drawFastHLine(8, y, 60, th->panel);
  y += 6;
  for (uint8_t i = 0; i < mp::HOST_TEXT_SLOTS && y + 10 < LEGEND_TOP; i++) {  // a line is 8 px
    const char* t = d.text[i];
    if (!*t) continue;
    for (int line = 0; line < 2 && *t && y + 10 < LEGEND_TOP; line++) {
      char part[13];
      const int n = wrap12(t, part);
      small(c, part, 2, y, line ? th->subtle : th->text);
      t += n;
      while (*t == ' ') t++;
      y += 10;
    }
    y += 4;
  }
  layer_legend(c, s);
}

// ------------------------------------------------ main display 320 x 170
// CONFIG view: the setting a key changes, shown under its label ("" = none).  *swatch = an LED colour to show.
const char* config_value(const UiState& s, mp::Action a, char* buf, size_t n, bool* swatch) {
  *swatch = false;
  if (mp::act_type(a) != mp::ActType::Sys) return "";
  switch (mp::SysCmd(mp::act_arg(a))) {
    case mp::SYS_MAIN_THEME: return mp::theme_name(s.main_theme);
    case mp::SYS_BAR_THEME: return mp::theme_name(s.bar_theme);
    case mp::SYS_LED_COLOR: *swatch = true; return mp::led_color_name(s.led_color);
    case mp::SYS_LED_UP:
    case mp::SYS_LED_DOWN: snprintf(buf, n, "%u/%u", s.led_level, mp::LED_LEVELS); return buf;
    case mp::SYS_BRIGHT_UP:
    case mp::SYS_BRIGHT_DOWN: snprintf(buf, n, "%u%%", (s.brightness * 100u + 127) / 255); return buf;
    case mp::SYS_LED_MODE: return mp::led_mode_name(mp::LedMode(s.led_mode));
    case mp::SYS_DISPLAYS_TOGGLE: return "on";  // nothing is drawn while they are off
    default: return "";
  }
}

// One key cell at row r, column k: its label, and on the CONFIG view the current value below it.
void key_cell(GFXcanvas16& c, const UiState& s, uint8_t r, uint8_t k) {
  static const int col_x[mp::COLS] = {6, 174, 246};
  const int x = col_x[k], y = 31 + r * 35, w = 68, h = 31;
  uint8_t from = 0;
  const mp::KeyDef* key = resolved_key(s, r, k, &from);
  const char* label = key ? key->label : "";
  const uint16_t kc = mp::LAYERS[from].color;
  const bool down = s.keys_down[r][k];
  c.fillRoundRect(x, y, w, h, 7, down ? kc : th->cell);
  if (!down) c.drawRoundRect(x, y, w, h, 7, scale(kc, from == s.base_layer ? 110 : 200));
  const uint16_t ink_label = down ? on_fill(*th) : (*label ? th->text : th->muted);

  char buf[8];
  bool swatch = false;
  const char* value = key && mp::View(s.view) == mp::View::Config ? config_value(s, key->action, buf, sizeof buf, &swatch) : "";
  if (!*value) return label_centered(c, label, x + w / 2, y + h / 2, w - 4, ink_label);
  label_centered(c, label, x + w / 2, y + 10, w - 4, ink_label);
  int vx = x + (w - 6 * int(strlen(value)) - (swatch ? 9 : 0)) / 2;
  if (swatch) {
    c.fillRect(vx, y + 21, 7, 7, mp::led_color565(s.led_color, mp::LAYERS[s.top_layer].color));
    c.drawRect(vx, y + 21, 7, 7, th->muted);  // a WHITE swatch on a light cell
    vx += 9;
  }
  small(c, value, vx, y + 21, down ? on_fill(*th) : th->subtle);
}

void render_main(GFXcanvas16& c, const UiState& s) {
  if (mp::is_host_view(mp::View(s.view))) return render_host_main(c, s);
  const mp::Layer& top = mp::LAYERS[s.top_layer];
  const uint16_t accent = top.color;
  c.fillScreen(th->bg);

  // header: layer name, layer dots, status
  c.fillRect(0, 0, 320, 26, th->panel);
  c.fillRect(0, 0, 5, 26, accent);
  text_at(c, top.name, 12, 20, &FreeSansBold12pt7b, ink(*th, accent));
  int dx = 150;
  for (uint8_t l = 0; l < mp::NUM_LAYERS; l++) {
    if (!mp::in_layer_cycle(l)) continue;
    if (l == mp::layer_group(s.base_layer))
      c.fillCircle(dx, 13, 4, mp::LAYERS[l].color);
    else
      c.drawCircle(dx, 13, 4, th->muted);
    dx += 13;
  }
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(s.leds & 0x02 ? ink(*th, WARN) : th->muted);
  c.setCursor(236, 10);
  c.print("CAPS");
  c.fillCircle(290, 13, 4, s.usb_mounted ? (s.usb_suspended ? WARN : GOOD) : FN_RED);
  c.setTextColor(th->subtle);
  c.setCursor(298, 10);
  c.print("USB");

  // key cells mirror the physical layout: 1 column left, 2 columns right
  for (uint8_t r = 0; r < mp::ROWS; r++)
    for (uint8_t k = 0; k < mp::COLS; k++) key_cell(c, s, r, k);

  // joystick or knob widget in the centre column
  const int cx = 124, cy = 92, R = 31;
  if (s.centre_encoder) {
    knob_widget(c, s, accent, cx, cy - 2, R - 3);
    knob_icon(c, cx - 22, 148, th->subtle);
  } else {
    c.fillCircle(cx, cy, R, th->cell);
    c.drawCircle(cx, cy, R, s.joy_button ? accent : th->muted);
    c.drawFastHLine(cx - R + 4, cy, 2 * R - 8, scale(th->muted, 140));
    c.drawFastVLine(cx, cy - R + 4, 2 * R - 8, scale(th->muted, 140));
    const int px = cx + int(s.joy_x * (R - 7)), py = cy - int(s.joy_y * (R - 7));
    c.drawLine(cx, cy, px, py, scale(accent, 150));
    c.fillCircle(px, py, s.joy_button ? 7 : 6, accent);
    joy_icon(c, s.joy_mode, cx - 20, 148, th->subtle);
  }
  c.setFont(nullptr);
  c.setTextColor(th->subtle);
  c.setCursor(cx - 4, 145);
  c.print(s.centre_encoder ? s.enc_label : joy_mode_name(s.joy_mode));
  if (s.macro_running) {
    c.setTextColor(ink(*th, WARN));
    c.setCursor(cx - 12, 160);
    c.print("macro...");
  }
}

// --------------------------------------------------- bar display 76 x 284
void render_bar(GFXcanvas16& c, const UiState& s) {
  if (mp::is_host_view(mp::View(s.view))) return render_host_bar(c, s);
  const mp::Layer& top = mp::LAYERS[s.top_layer];
  c.fillScreen(th->bg);

  // crescent emblem + wordmark
  c.fillCircle(38, 21, 14, top.color);
  c.fillCircle(45, 15, 12, th->bg);
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(th->subtle);
  c.setCursor(17, 42);
  c.print("NYXILAB");

  // base layers as pills (one per layer, named after the page shown), FN as a banner; pills shrink so
  // that both fit above y = 180
  int n_base = 0;
  for (uint8_t l = 0; l < mp::NUM_LAYERS; l++) n_base += mp::in_layer_cycle(l);
  const int pitch = n_base > 3 ? (180 - 56 - 26) / n_base : 32, ph = pitch - 6 < 26 ? pitch - 4 : 26;
  int y = 56;
  for (uint8_t l = 0; l < mp::NUM_LAYERS; l++) {
    if (!mp::in_layer_cycle(l)) continue;
    const mp::Layer& L = mp::LAYERS[l];
    if (l == mp::layer_group(s.base_layer)) {
      c.fillRoundRect(4, y, 68, ph, ph / 2, L.color);
      label_centered(c, mp::LAYERS[s.base_layer].name, 38, y + ph / 2, 64, on_fill(*th));
    } else {
      c.drawRoundRect(4, y, 68, ph, ph / 2, th->muted);
      label_centered(c, L.name, 38, y + ph / 2, 64, th->subtle);
    }
    y += pitch;
  }
  // FN held, or the CONFIG view: a banner under the pills
  if (s.top_layer == mp::FN_LAYER || mp::View(s.view) == mp::View::Config) {
    c.fillRoundRect(4, y + 2, 68, 22, 6, top.color);
    label_centered(c, top.name, 38, y + 13, 64, on_fill(*th));
  }

  // joystick mode or knob binding (small built-in font from here on)
  const int jy = 200;
  c.drawFastHLine(8, jy - 18, 60, th->panel);
  if (s.centre_encoder)
    knob_icon(c, 17, jy, ink(*th, top.color));
  else
    joy_icon(c, s.joy_mode, 18, jy, ink(*th, top.color));
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(th->text);
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
      c.drawRoundRect(x, 222, 21, 13, 4, th->muted);
    c.setTextColor(on ? on_fill(*th) : th->muted);
    c.setCursor(x + 2, 225);
    c.print(kLocks[i]);
  }

  // key activity sparkline (presses per second)
  uint8_t peak = 4;
  for (uint8_t v : s.activity) peak = v > peak ? v : peak;
  const int gy = 280, gh = 30;
  c.drawFastHLine(2, gy, 72, th->panel);
  for (int i = 0; i < ACTIVITY_SAMPLES; i++) {
    const int h = s.activity[i] * gh / peak;
    if (h > 0) c.fillRect(2 + i * 2, gy - h, 1, h, scale(top.color, uint8_t(90 + i * 4)));
  }
  c.setTextColor(th->muted);
  c.setCursor(2, 244);
  c.print(s.usb_mounted ? (s.usb_suspended ? "host asleep" : "keys/s") : "no USB");
}

}  // namespace

void render_main_screen(GFXcanvas16& c, const UiState& s) {
  th = &theme(s.main_theme);
  render_main(c, s);
}

void render_bar_screen(GFXcanvas16& c, const UiState& s) {
  th = &theme(s.bar_theme);
  render_bar(c, s);
}

}  // namespace ui
