// Joystick signal processing: calibration, radial dead zone, response curve,
// and the three output modes (mouse, scroll, arrow keys).  Pure C++.
#pragma once
#include <stdint.h>

#include "keycodes.h"

namespace mp {

struct JoyCal {
  uint16_t cx = 2048, cy = 2048;               // rest position (sampled at boot / on CAL)
  uint16_t min_x = 400, max_x = 3700;          // expands automatically while in use
  uint16_t min_y = 400, max_y = 3700;
};

struct JoyConfig {
  float deadzone = 0.10f;      // fraction of full deflection
  float curve = 1.9f;          // >1 = finer control near the centre
  float mouse_speed = 1100.f;  // pixels / s at full deflection
  float scroll_speed = 16.f;   // wheel detents / s at full deflection
  float arrow_on = 0.55f;      // arrow key press threshold
  float arrow_off = 0.38f;     // release threshold (hysteresis)
  bool invert_x = false;
  bool invert_y = false;
  bool swap_xy = false;
};

struct JoyOutput {
  int8_t dx = 0, dy = 0;      // mouse movement (screen y grows downwards)
  int8_t wheel = 0, pan = 0;  // scroll (wheel > 0 = up)
  uint8_t keys[2] = {};       // arrow keys to hold
  uint8_t n_keys = 0;
};

class Joystick {
 public:
  void set_config(const JoyConfig& c) { cfg_ = c; }
  const JoyConfig& config() const { return cfg_; }
  void set_cal(const JoyCal& c) { cal_ = c; }
  const JoyCal& cal() const { return cal_; }
  void calibrate_center(uint16_t raw_x, uint16_t raw_y);

  // raw_x / raw_y are 12-bit ADC readings.  dt in seconds.
  void update(uint16_t raw_x, uint16_t raw_y, float dt, JoyMode mode, JoyOutput& out);

  // processed position in -1..1 (x right, y up) after dead zone and curve, for the UI
  float x() const { return x_; }
  float y() const { return y_; }
  float raw_nx() const { return nx_; }
  float raw_ny() const { return ny_; }

  static float normalize(uint16_t raw, uint16_t lo, uint16_t mid, uint16_t hi);

 private:
  JoyConfig cfg_;
  JoyCal cal_;
  float nx_ = 0, ny_ = 0;  // normalised, before dead zone
  float x_ = 0, y_ = 0;    // after dead zone + curve
  float acc_x_ = 0, acc_y_ = 0, acc_w_ = 0, acc_p_ = 0;
  bool arrow_[4] = {};  // right, left, up, down
};

}  // namespace mp
