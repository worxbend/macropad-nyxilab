// Detent-synchronised quadrature decoder for the KY-040 (EC11) encoder.
// Pure C++ (no Arduino), unit-tested in test/test_encoder.
//
// Every valid Gray-code transition moves a counter by +-1; a step is reported
// only when the contacts are back in a rest position, so contact bounce and
// half-turned knobs never produce steps.  CW = channel A (CLK) changes first.
#pragma once
#include <stdint.h>

class QuadDecoder {
 public:
  // 4 transitions per detent (KY-040 / EC11 with 20 detents) or 2 (half-step encoders)
  explicit QuadDecoder(uint8_t steps_per_detent = 4) : spd_(steps_per_detent == 2 ? 2 : 4) {}

  void reset(bool a, bool b) {
    prev_ = state(a, b);
    acc_ = 0;
  }

  // Feed the pin levels (true = high) after any change.  Returns +1 for one detent
  // clockwise, -1 counter-clockwise, 0 otherwise.  Safe to call from an interrupt.
  int8_t update(bool a, bool b) {
    static const int8_t kDir[16] = {0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0};
    const uint8_t s = state(a, b);
    acc_ = int8_t(acc_ + kDir[(prev_ << 2) | s]);
    prev_ = s;
    if (s == 3 || (spd_ == 2 && s == 0)) {  // detent: both contacts open (or closed, half-step)
      const int8_t out = acc_ >= spd_ / 2 ? 1 : (acc_ <= -spd_ / 2 ? -1 : 0);
      acc_ = 0;
      return out;
    }
    return 0;
  }

 private:
  static uint8_t state(bool a, bool b) { return uint8_t((a ? 2 : 0) | (b ? 1 : 0)); }
  uint8_t spd_;
  uint8_t prev_ = 3;
  int8_t acc_ = 0;
};
