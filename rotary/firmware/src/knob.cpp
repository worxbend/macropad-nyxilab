#include "knob.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "hw/settings.h"

QuadDecoder KnobCentre::s_quad(cfg::ENC_STEPS_PER_DETENT);
volatile int32_t KnobCentre::s_steps = 0;

void KnobCentre::isr() { s_steps += s_quad.update(digitalRead(cfg::ENC_A_PIN), digitalRead(cfg::ENC_B_PIN)); }

bool KnobCentre::reversed() const { return cfg::ENC_REVERSE != (saved_ && saved_[0] != 0); }

void KnobCentre::begin(mp::Engine& engine, uint8_t* saved) {
  engine_ = &engine;
  saved_ = saved;
  pinMode(cfg::ENC_A_PIN, INPUT_PULLUP);
  pinMode(cfg::ENC_B_PIN, INPUT_PULLUP);
  pinMode(cfg::ENC_SW_PIN, INPUT_PULLUP);
  s_quad.reset(digitalRead(cfg::ENC_A_PIN), digitalRead(cfg::ENC_B_PIN));
  attachInterrupt(digitalPinToInterrupt(cfg::ENC_A_PIN), isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(cfg::ENC_B_PIN), isr, CHANGE);
  engine.set_encoder_presets(mp::ENC_PRESETS, mp::NUM_ENC_PRESETS);
  const auto& st = hw::settings();
  for (uint8_t l = 0; l < mp::MAX_LAYERS; l++) engine.set_enc_preset(l, st.centre_modes[l]);
}

void KnobCentre::on_mode(uint8_t mode, uint32_t now) {
  const uint8_t base = engine_->base_layer();
  if (mode == mp::ENC_CYCLE)
    engine_->cycle_enc_preset(base);
  else
    engine_->set_enc_preset(base, mode);  // a preset index, or ENC_LAYER_DEFAULT
  hw::settings().centre_modes[base] = engine_->enc_preset(base);
  app::settings_changed(now);
}

bool KnobCentre::on_sys(mp::SysCmd cmd, uint32_t now) {
  if (cmd != mp::SYS_ENC_REVERSE) return false;
  saved_[0] = !saved_[0];
  app::settings_changed(now);
  return true;
}

void KnobCentre::task(uint32_t now) {
  noInterrupts();
  int32_t steps = s_steps;
  s_steps = 0;
  interrupts();
  if (reversed()) steps = -steps;
  if (steps) {
    app::activity(now);
    app::hit(now);
  }
  for (; steps > 0; steps--) engine_->encoder_step(+1, now);
  for (; steps < 0; steps++) engine_->encoder_step(-1, now);

  // push switch: 4 equal samples 2 ms apart
  if (now - last_sw_ms_ < 2) return;
  last_sw_ms_ = now;
  const bool sw_raw = digitalRead(cfg::ENC_SW_PIN) == LOW;
  if (sw_raw == sw_) {
    sw_count_ = 0;
  } else if (++sw_count_ >= 4) {
    sw_ = sw_raw;
    sw_count_ = 0;
    engine_->encoder_button(sw_, now);
    app::activity(now);
    if (sw_) app::hit(now);
  }
}

void KnobCentre::fill_ui(ui::UiState& u) {
  u.centre_encoder = true;
  u.enc_pos = engine_->encoder_position();
  u.enc_press = engine_->encoder_pressed();
  u.enc_label = engine_->encoder_binding().label;
}

void KnobCentre::info() {
  Serial.printf("knob %s pos=%ld reversed=%d\n", engine_->encoder_binding().label, (long)engine_->encoder_position(),
                reversed());
}

bool KnobCentre::console(const char* line) {
  if (strcmp(line, "enc") != 0) return false;
  for (uint32_t t0 = millis(); millis() - t0 < 5000;) {
    Serial.printf("CLK=%d DT=%d SW=%d  pos=%ld\n", digitalRead(cfg::ENC_A_PIN), digitalRead(cfg::ENC_B_PIN),
                  digitalRead(cfg::ENC_SW_PIN) == LOW, (long)engine_->encoder_position());
    task(millis());
    delay(50);
  }
  return true;
}
