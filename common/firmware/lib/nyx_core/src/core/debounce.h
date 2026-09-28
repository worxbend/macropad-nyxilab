// Per-key "deferred" debounce: a key changes state only after its raw reading
// has been stable for `ms` milliseconds.  Pure logic, unit-tested natively.
#pragma once
#include <stdint.h>

#include "keymap.h"

namespace mp {

struct KeyEvent {
  uint8_t row;
  uint8_t col;
  bool pressed;
  uint32_t time;
};

class Debouncer {
 public:
  explicit Debouncer(uint8_t ms = 5) : ms_(ms) {}

  // Feed one full matrix scan (raw[r] bit c = key down).  Calls emit(event)
  // for every debounced change and returns the number of events.
  template <typename F>
  uint8_t update(const uint8_t (&raw)[ROWS], uint32_t now, F&& emit) {
    uint8_t n = 0;
    for (uint8_t r = 0; r < ROWS; r++) {
      for (uint8_t c = 0; c < COLS; c++) {
        const bool down = (raw[r] >> c) & 1;
        const bool stable = (state_[r] >> c) & 1;
        if (down == stable) {
          pending_[r][c] = false;
          continue;
        }
        if (!pending_[r][c]) {
          pending_[r][c] = true;
          since_[r][c] = now;
        } else if (uint32_t(now - since_[r][c]) >= ms_) {
          state_[r] = down ? (state_[r] | (1u << c)) : (state_[r] & ~(1u << c));
          pending_[r][c] = false;
          emit(KeyEvent{r, c, down, now});
          n++;
        }
      }
    }
    return n;
  }

  bool is_down(uint8_t r, uint8_t c) const { return (state_[r] >> c) & 1; }

 private:
  uint8_t ms_;
  uint8_t state_[ROWS] = {};
  bool pending_[ROWS][COLS] = {};
  uint32_t since_[ROWS][COLS] = {};
};

}  // namespace mp
