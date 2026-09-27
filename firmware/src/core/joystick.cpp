#include "joystick.h"

#include <math.h>

namespace mp {

float Joystick::normalize(uint16_t raw, uint16_t lo, uint16_t mid, uint16_t hi) {
  if (raw >= mid) {
    const float span = float(hi) - float(mid);
    return span > 1.f ? fminf((float(raw) - float(mid)) / span, 1.f) : 0.f;
  }
  const float span = float(mid) - float(lo);
  return span > 1.f ? fmaxf((float(raw) - float(mid)) / span, -1.f) : 0.f;
}

void Joystick::calibrate_center(uint16_t raw_x, uint16_t raw_y) {
  cal_.cx = raw_x;
  cal_.cy = raw_y;
  acc_x_ = acc_y_ = acc_w_ = acc_p_ = 0;
}

static int8_t take_int(float& acc) {
  float whole = truncf(acc);
  if (whole > 127.f) whole = 127.f;
  if (whole < -127.f) whole = -127.f;
  acc -= whole;
  return int8_t(whole);
}

void Joystick::update(uint16_t raw_x, uint16_t raw_y, float dt, JoyMode mode, JoyOutput& out) {
  out = JoyOutput{};
  // learn the real travel of this particular stick
  if (raw_x < cal_.min_x) cal_.min_x = raw_x;
  if (raw_x > cal_.max_x) cal_.max_x = raw_x;
  if (raw_y < cal_.min_y) cal_.min_y = raw_y;
  if (raw_y > cal_.max_y) cal_.max_y = raw_y;

  float nx = normalize(raw_x, cal_.min_x, cal_.cx, cal_.max_x);
  float ny = normalize(raw_y, cal_.min_y, cal_.cy, cal_.max_y);
  if (cfg_.swap_xy) {
    const float t = nx;
    nx = ny;
    ny = t;
  }
  if (cfg_.invert_x) nx = -nx;
  if (cfg_.invert_y) ny = -ny;
  nx_ = nx;
  ny_ = ny;

  // radial dead zone + power curve, keeping the direction
  const float mag = sqrtf(nx * nx + ny * ny);
  float x = 0, y = 0;
  if (mag > cfg_.deadzone) {
    float s = (fminf(mag, 1.f) - cfg_.deadzone) / (1.f - cfg_.deadzone);
    s = powf(s, cfg_.curve);
    x = nx / mag * s;
    y = ny / mag * s;
  }
  x_ = x;
  y_ = y;

  switch (mode) {
    case JoyMode::Mouse:
      acc_x_ += x * cfg_.mouse_speed * dt;
      acc_y_ += -y * cfg_.mouse_speed * dt;  // stick up = pointer up (screen y down)
      out.dx = take_int(acc_x_);
      out.dy = take_int(acc_y_);
      break;
    case JoyMode::Scroll:
      acc_w_ += y * cfg_.scroll_speed * dt;
      acc_p_ += x * cfg_.scroll_speed * dt;
      out.wheel = take_int(acc_w_);
      out.pan = take_int(acc_p_);
      break;
    case JoyMode::Arrows: {
      // per-axis hysteresis on the un-curved position so the feel is predictable
      const float v[4] = {nx, -nx, ny, -ny};
      static const uint8_t kKeys[4] = {hid::RIGHT, hid::LEFT, hid::UP, hid::DOWN};
      for (int i = 0; i < 4; i++) {
        if (!arrow_[i] && v[i] > cfg_.arrow_on) arrow_[i] = true;
        if (arrow_[i] && v[i] < cfg_.arrow_off) arrow_[i] = false;
        if (arrow_[i] && out.n_keys < 2) out.keys[out.n_keys++] = kKeys[i];
      }
      break;
    }
    default:
      break;
  }
  if (mode != JoyMode::Arrows)
    for (bool& a : arrow_) a = false;
  if (mode != JoyMode::Mouse) acc_x_ = acc_y_ = 0;
  if (mode != JoyMode::Scroll) acc_w_ = acc_p_ = 0;
}

}  // namespace mp
