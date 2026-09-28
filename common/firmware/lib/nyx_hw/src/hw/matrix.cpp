#include "matrix.h"

#include <Arduino.h>

#include "config.h"

namespace hw {

void matrix_init() {
  for (uint8_t p : cfg::ROW_PINS) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
  }
  for (uint8_t p : cfg::COL_PINS) pinMode(p, INPUT_PULLUP);
}

// COL2ROW: drive one row low, a pressed key pulls its column low through the diode.
void matrix_scan(uint8_t (&raw)[mp::ROWS]) {
  for (uint8_t r = 0; r < mp::ROWS; r++) {
    digitalWrite(cfg::ROW_PINS[r], LOW);
    delayMicroseconds(8);  // hand-wired lines + internal pull-ups need a moment
    uint8_t bits = 0;
    for (uint8_t c = 0; c < mp::COLS; c++)
      if (digitalRead(cfg::COL_PINS[c]) == LOW) bits |= 1u << c;
    raw[r] = bits;
    digitalWrite(cfg::ROW_PINS[r], HIGH);
    delayMicroseconds(4);
  }
}

bool matrix_boot_combo_held() {
  uint8_t raw[mp::ROWS];
  for (int i = 0; i < 3; i++) {  // must be stable over three scans
    matrix_scan(raw);
    for (const auto& k : mp::BOOT_COMBO)
      if (!((raw[k[0]] >> k[1]) & 1)) return false;
    delay(5);
  }
  return true;
}

}  // namespace hw
