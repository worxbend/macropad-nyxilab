// Which parts of a frame differ from the frame the panel already shows.
// A full frame is ~23 ms of blocking SPI for the 1.9" panel at 37.5 MHz; a knob detent changes 2 % of it.
#pragma once
#include <stdint.h>

namespace ui {

// 16 x 8 tiles: a quarter of the hashes of 8 x 8 for a few percent more pixels pushed (measured on the
// real layouts: knob detent, key press, host dashboard, layer change).
constexpr int TILE_W = 16, TILE_H = 8;

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
};

constexpr int tiles_across(int w) { return (w + TILE_W - 1) / TILE_W; }
constexpr int tile_count(int w, int h) { return tiles_across(w) * ((h + TILE_H - 1) / TILE_H); }

// Hashes every tile of a w x h RGB565 frame, stores the hashes in `shown` (tile_count(w, h) entries) and
// calls push(Rect) for what changed: per tile row one rectangle from its first to its last changed tile,
// grown downwards while the next tile row changed over the same columns.
// shortcut: a 32-bit hash, not a copy of the frame (the Pico has no RAM for a second one); a
// colliding tile stays stale until it changes again. Upgrade if a stale tile is ever seen.
template <typename Push>
void for_each_changed_rect(const uint16_t* frame, int w, int h, uint32_t* shown, Push&& push) {
  Rect open;  // the rectangle still growing downwards; w = 0: none
  const auto flush = [&] {
    if (open.w) push(open);
    open = Rect{};
  };
  for (int y = 0; y < h; y += TILE_H) {
    const int rows = h - y < TILE_H ? h - y : TILE_H;
    int first = w, end = 0;  // changed columns of this tile row: [first, end)
    for (int x = 0; x < w; x += TILE_W, shown++) {
      const int cols = w - x < TILE_W ? w - x : TILE_W;
      uint32_t hash = 2166136261u;  // FNV-1a: the order of the pixels matters
      for (int r = 0; r < rows; r++) {
        const uint16_t* px = frame + (y + r) * w + x;
        for (int c = 0; c < cols; c++) hash = (hash ^ px[c]) * 16777619u;
      }
      if (hash == *shown) continue;
      *shown = hash;
      if (first == w) first = x;
      end = x + cols;
    }
    const bool grows = open.w && end > first && open.x == first && open.w == end - first;
    if (grows) {
      open.h = int16_t(open.h + rows);
      continue;
    }
    flush();
    if (end > first) open = {int16_t(first), int16_t(y), int16_t(end - first), int16_t(rows)};
  }
  flush();
}

}  // namespace ui
