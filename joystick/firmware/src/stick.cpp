#include "stick.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "hw/settings.h"
#include "hw/usb_hid.h"

using mp::JoyMode;

static_assert(sizeof(mp::JoyCal) <= hw::CENTRE_BYTES, "JoyCal must fit the settings' centre bytes");

uint16_t JoystickCentre::read_adc_avg(uint8_t pin) {
  uint32_t sum = 0;
  for (int i = 0; i < 4; i++) sum += analogRead(pin);
  return uint16_t(sum / 4);
}

void JoystickCentre::begin(mp::Engine& engine, uint8_t* saved) {
  engine_ = &engine;
  saved_ = saved;
  pinMode(cfg::JOY_SW_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  mp::JoyConfig jc;
  jc.invert_x = cfg::JOY_INVERT_X;
  jc.invert_y = cfg::JOY_INVERT_Y;
  jc.swap_xy = cfg::JOY_SWAP_XY;
  joy_.set_config(jc);
  mp::JoyCal cal;
  memcpy(&cal, saved, sizeof cal);
  if (cal.max_x == 0) cal = mp::JoyCal{};  // first boot: the bytes are zero
  joy_.set_cal(cal);
  // the stick is at rest while plugging in: take the centre from here
  joy_.calibrate_center(read_adc_avg(cfg::JOY_X_PIN), read_adc_avg(cfg::JOY_Y_PIN));
}

JoyMode JoystickCentre::mode() const {
  const auto& st = hw::settings();
  const uint8_t base = engine_->base_layer(), top = engine_->top_layer();
  const JoyMode top_mode = mp::LAYERS[top].centre.joy;
  if (top_mode != JoyMode::Inherit && top != base) return top_mode;
  const uint8_t user = st.centre_modes[base];
  if (user < uint8_t(JoyMode::Count)) return JoyMode(user);
  const JoyMode def = mp::LAYERS[base].centre.joy;
  return def == JoyMode::Inherit ? JoyMode::Mouse : def;
}

void JoystickCentre::on_mode(uint8_t mode_arg, uint32_t now) {
  auto& st = hw::settings();
  const uint8_t base = engine_->base_layer();
  if (mode_arg == uint8_t(JoyMode::Cycle)) {
    st.centre_modes[base] = uint8_t((uint8_t(mode()) + 1) % uint8_t(JoyMode::Count));
  } else if (mode_arg < uint8_t(JoyMode::Count)) {
    st.centre_modes[base] = mode_arg;
  } else {
    return;
  }
  app::settings_changed(now);
}

void JoystickCentre::calibrate(uint32_t now) {
  joy_.calibrate_center(read_adc_avg(cfg::JOY_X_PIN), read_adc_avg(cfg::JOY_Y_PIN));
  const mp::JoyCal cal = joy_.cal();
  memcpy(saved_, &cal, sizeof cal);
  app::settings_changed(now);
}

bool JoystickCentre::on_sys(mp::SysCmd cmd, uint32_t now) {
  if (cmd != mp::SYS_JOY_CALIBRATE) return false;
  calibrate(now);
  return true;
}

void JoystickCentre::task(uint32_t now) {
  if (now - last_ms_ < cfg::JOY_PERIOD_MS) return;
  const float dt = (now - last_ms_) / 1000.f;
  last_ms_ = now;

  const JoyMode m = mode();
  mp::JoyOutput out;
  joy_.update(read_adc_avg(cfg::JOY_X_PIN), read_adc_avg(cfg::JOY_Y_PIN), dt > 0.1f ? 0.1f : dt, m, out);
  if (out.dx || out.dy || out.wheel || out.pan || out.n_keys) app::activity(now);
  hw::usb_add_mouse(out.dx, out.dy, out.wheel, out.pan);

  // stick click: debounced, meaning depends on the mode
  const bool sw_raw = digitalRead(cfg::JOY_SW_PIN) == LOW;
  if (sw_raw != sw_) {
    if (++sw_count_ >= 3) {
      sw_ = sw_raw;
      sw_count_ = 0;
      app::activity(now);
      if (sw_) app::hit(now);
    }
  } else {
    sw_count_ = 0;
  }
  uint8_t keys[3];
  uint8_t nk = 0;
  for (uint8_t i = 0; i < out.n_keys; i++) keys[nk++] = out.keys[i];
  uint8_t buttons = 0;
  if (sw_) {
    if (m == JoyMode::Mouse) buttons = mp::MB_LEFT;
    if (m == JoyMode::Scroll) buttons = mp::MB_MIDDLE;
    if (m == JoyMode::Arrows) keys[nk++] = mp::hid::ENTER;
  }
  hw::usb_set_extra_buttons(buttons);
  engine_->set_extra_keys(keys, nk);
}

void JoystickCentre::fill_ui(ui::UiState& u) {
  u.centre_encoder = false;
  u.joy_x = joy_.x();
  u.joy_y = joy_.y();
  u.joy_mode = uint8_t(mode());
  u.joy_button = sw_;
}

void JoystickCentre::info() {
  const auto& c = joy_.cal();
  Serial.printf("joy mode=%u cal centre=(%u,%u) x=[%u..%u] y=[%u..%u]\n", unsigned(mode()), c.cx, c.cy, c.min_x,
                c.max_x, c.min_y, c.max_y);
}

bool JoystickCentre::console(const char* line) {
  if (!strcmp(line, "joy")) {
    for (uint32_t t0 = millis(); millis() - t0 < 5000;) {
      Serial.printf("raw x=%4u y=%4u  norm x=%+.2f y=%+.2f  sw=%d\n", read_adc_avg(cfg::JOY_X_PIN),
                    read_adc_avg(cfg::JOY_Y_PIN), joy_.raw_nx(), joy_.raw_ny(), digitalRead(cfg::JOY_SW_PIN) == LOW);
      delay(100);
    }
    return true;
  }
  if (!strcmp(line, "cal")) {
    calibrate(millis());
    Serial.println("joystick centre re-calibrated");
    return true;
  }
  return false;
}
