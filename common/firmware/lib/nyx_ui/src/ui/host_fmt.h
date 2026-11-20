// Text for the host dashboard: numbers from the daemon formatted into the caller's fixed buffers.
// Header-only and free of GFX, so the mocked-hardware tests can pin every string (render.cpp has no test).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/host_proto.h"
#include "core/keymap.h"

namespace ui {

// "12.3" from tenths, without float printf
inline void tenths(char* o, size_t n, uint32_t v10, const char* suffix) {
  snprintf(o, n, "%lu.%lu%s", (unsigned long)(v10 / 10), (unsigned long)(v10 % 10), suffix);
}

inline void fmt_rate(char* o, size_t n, uint32_t bps) {
  if (bps < 1000) snprintf(o, n, "%luB", (unsigned long)bps);
  else if (bps < 1000000) tenths(o, n, bps / 100, "K");
  else if (bps < 1000000000) tenths(o, n, bps / 100000, "M");
  else tenths(o, n, bps / 100000000, "G");
}

inline void fmt_pct(char* o, size_t n, uint8_t pct) {
  if (pct <= 100) snprintf(o, n, "%u%%", pct);
  else snprintf(o, n, "--");
}

inline void fmt_temp(char* o, size_t n, int16_t c10) {
  if (c10 == mp::HOST_NO_TEMP) snprintf(o, n, "--C");
  else snprintf(o, n, "%dC", (c10 + (c10 >= 0 ? 5 : -5)) / 10);
}

inline void fmt_uptime(char* o, size_t n, uint32_t s) {
  const unsigned long d = s / 86400, h = s / 3600 % 24, m = s / 60 % 60;
  if (d) snprintf(o, n, "%lud %luh", d, h);
  else if (h) snprintf(o, n, "%luh %lum", h, m);
  else snprintf(o, n, "%lum", m);
}

// The host name in the dashboard header: 22 characters of 6 px from x = 100 end 6 px short of the clock.
// A longer layer name and the page dots leave room for fewer (`chars`).
constexpr int HEADER_HOST_CHARS = 22;
inline void fmt_header_host(char o[HEADER_HOST_CHARS + 1], const char* hostname, int chars = HEADER_HOST_CHARS) {
  if (chars < 0) chars = 0;
  if (chars > HEADER_HOST_CHARS) chars = HEADER_HOST_CHARS;
  snprintf(o, HEADER_HOST_CHARS + 1, "%.*s", chars, hostname);
}

// Copies the next line of a 12-column text slot into `part` and returns how many characters of `t`
// it took: at most 12, breaking at the last space that fits, if there is one.
inline int wrap_cols(const char* t, char* part, int cols) {
  int n = int(strlen(t));
  if (n > cols) {
    n = cols;
    for (int i = cols; i > 0; i--)
      if (t[i] == ' ') {
        n = i;
        break;
      }
  }
  memcpy(part, t, n);
  part[n] = 0;
  return n;
}
inline int wrap12(const char* t, char part[13]) { return wrap_cols(t, part, 12); }

// Up to `max_lines` lines of at most `cols` (<= WRAP_MAX_COLS) characters; returns how many.  When text is
// left over, the last line ends in "...".
constexpr int WRAP_MAX_COLS = 53;  // 320 px of the 6 px font
inline int wrap_lines(const char* t, int cols, int max_lines, char (*out)[WRAP_MAX_COLS + 1]) {
  int n = 0;
  while (*t && n < max_lines) {
    t += wrap_cols(t, out[n++], cols);
    while (*t == ' ') t++;
  }
  if (*t && n) {
    char* last = out[n - 1];
    int len = int(strlen(last));
    if (len > cols - 3) len = cols - 3;
    while (len > 0 && last[len - 1] == ' ') len--;
    memcpy(last + len, "...", 4);
  }
  return n;
}

// The longest prefix of `s` that is at most `max_w` wide by `width(const char*)`, cut with "..." (also when
// `s` does not fit the buffer).  For the proportional fonts, whose width only the canvas can measure.
template <class Width>
inline void fit_width(char* o, size_t n, const char* s, int max_w, Width width) {
  snprintf(o, n, "%s", s);
  size_t len = strlen(o);
  if (len == strlen(s) && width(o) <= max_w) return;
  while (len > 0) {
    len--;
    while (len > 0 && o[len - 1] == ' ') len--;
    if (len + 4 > n) continue;
    memcpy(o + len, "...", 4);
    if (width(o) <= max_w) return;
  }
}

// ------------------------------------------------------------------ SWAY page
inline const char* sway_layout_name(uint8_t layout) {
  static const char* const kNames[] = {"", "splith", "splitv", "tabbed", "stacked"};
  return layout < sizeof kNames / sizeof kNames[0] ? kNames[layout] : "";
}

inline void fmt_windows(char* o, size_t n, uint8_t windows) {
  snprintf(o, n, "%u window%s", windows, windows == 1 ? "" : "s");
}

// ---------------------------------------------------------------- PLAYER page
inline void fmt_clock_time(char* o, size_t n, uint16_t s, bool hours) {  // at most "18:12:15"
  const unsigned long h = s / 3600, m = s / 60 % 60, sec = s % 60;
  if (hours) snprintf(o, n, "%lu:%02lu:%02lu", h, m, sec);
  else snprintf(o, n, "%lu:%02lu", s / 60ul, sec);
}

// "m:ss / m:ss", h:mm:ss on both sides from an hour on; only the position when the length is unknown (0).
inline void fmt_progress(char* o, size_t n, uint16_t pos, uint16_t len) {
  const bool hours = (pos > len ? pos : len) >= 3600;
  char a[9], b[9];
  fmt_clock_time(a, sizeof a, pos, hours);
  if (!len) {
    snprintf(o, n, "%s", a);
    return;
  }
  fmt_clock_time(b, sizeof b, len, hours);
  snprintf(o, n, "%s / %s", a, b);
}

// ------------------------------------------------------------ HOST page legend
struct HostPage {
  uint8_t index, count;  // 0-based place among the pages of the layer's group
};
inline HostPage host_page(uint8_t layer) {
  const uint8_t group = mp::layer_group(layer);
  HostPage p{0, 0};
  for (uint8_t l = 0; l < mp::NUM_LAYERS; l++) {
    if (mp::layer_group(l) != group) continue;
    if (l < layer) p.index++;
    p.count++;
  }
  return p;
}

inline const char* page_short_name(mp::View v) {
  switch (v) {
    case mp::View::Host: return "SYS";
    case mp::View::Sway: return "SWAY";
    case mp::View::Player: return "PLAY";
    default: return "";
  }
}

// "2/3 SWAY"
inline void fmt_host_page(char* o, size_t n, uint8_t layer) {
  const HostPage p = host_page(layer);
  snprintf(o, n, "%u/%u %s", p.index + 1, p.count, page_short_name(mp::LAYERS[layer].view));
}

}  // namespace ui
