// Text for the host dashboard: numbers from the daemon formatted into the caller's fixed buffers.
// Header-only and free of GFX, so the mocked-hardware tests can pin every string (render.cpp has no test).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/host_proto.h"

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
constexpr int HEADER_HOST_CHARS = 22;
inline void fmt_header_host(char o[HEADER_HOST_CHARS + 1], const char* hostname) {
  snprintf(o, HEADER_HOST_CHARS + 1, "%.*s", HEADER_HOST_CHARS, hostname);
}

// Copies the next line of a 12-column text slot into `part` and returns how many characters of `t`
// it took: at most 12, breaking at the last space that fits, if there is one.
inline int wrap12(const char* t, char part[13]) {
  int n = int(strlen(t));
  if (n > 12) {
    n = 12;
    for (int i = 12; i > 0; i--)
      if (t[i] == ' ') {
        n = i;
        break;
      }
  }
  memcpy(part, t, n);
  part[n] = 0;
  return n;
}

}  // namespace ui
