// The joystick edition's centre control: the KY-023 thumbstick.
//
// Reads the two pots on the ADC, turns them into mouse motion, scroll or arrow
// keys (per layer, FN + JOY cycles), handles the stick click and keeps the
// calibration in the settings' centre bytes.
#pragma once
#include "app/app.h"
#include "joystick.h"

class JoystickCentre : public app::Centre {
 public:
  void begin(mp::Engine& engine, uint8_t* saved) override;
  void task(uint32_t now) override;
  void fill_ui(ui::UiState& u) override;
  void on_mode(uint8_t mode, uint32_t now) override;
  bool on_sys(mp::SysCmd cmd, uint32_t now) override;
  bool console(const char* line) override;
  void info() override;
  const char* help() const override { return "joy (raw stick for 5 s) | cal"; }

  mp::JoyMode mode() const;  // what the stick does right now

 private:
  void calibrate(uint32_t now);
  static uint16_t read_adc_avg(uint8_t pin);

  mp::Engine* engine_ = nullptr;
  uint8_t* saved_ = nullptr;  // JoyCal, in the flash settings
  mp::Joystick joy_;
  uint32_t last_ms_ = 0;
  uint8_t sw_count_ = 0;
  bool sw_ = false;
};
