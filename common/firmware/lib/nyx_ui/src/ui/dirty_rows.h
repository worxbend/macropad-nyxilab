// Which rows of a frame differ from the frame the panel already shows.
// A full frame is ~25 ms of blocking SPI for the 1.9" panel; a key press changes a fifth of its rows.
#pragma once
#include <stdint.h>

namespace ui {

struct RowSpan {
  int16_t first = 0;
  int16_t count = 0;  // 0 = nothing changed
};

// Hashes every row of a w x h RGB565 frame, stores the hashes in `shown` (h entries) and returns
// the smallest band of rows that covers every row whose hash differs from the one in `shown`.
// shortcut: a 32-bit hash, not a copy of the frame (the Pico has no RAM for a second one); a
// colliding row stays stale until it changes again. Upgrade if a stale row is ever seen.
inline RowSpan changed_rows(const uint16_t* frame, int w, int h, uint32_t* shown) {
  int first = h, last = -1;
  for (int y = 0; y < h; y++) {
    uint32_t hash = 2166136261u;  // FNV-1a: the order of the pixels matters
    for (const uint16_t* px = frame + y * w; px < frame + (y + 1) * w; px++) hash = (hash ^ *px) * 16777619u;
    if (hash == shown[y]) continue;
    shown[y] = hash;
    if (first == h) first = y;
    last = y;
  }
  RowSpan rows;
  if (last >= 0) {
    rows.first = int16_t(first);
    rows.count = int16_t(last - first + 1);
  }
  return rows;
}

}  // namespace ui
