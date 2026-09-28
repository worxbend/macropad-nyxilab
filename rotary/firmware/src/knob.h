// The rotary edition's centre control: the KY-040 encoder under the printed knob.
//
// Pin-change interrupts feed the quadrature decoder; every detent becomes one
// tap of the layer's binding (or the preset picked with FN + KNOB), the push
// switch is held like a key.  The turning direction can be flipped with FN + REV
// and is kept in the settings' centre bytes.
#pragma once
#include "app/app.h"
#include "presets.h"
#include "quad_decoder.h"

class KnobCentre : public app::Centre {
 public:
  void begin(mp::Engine& engine, uint8_t* saved) override;
  void task(uint32_t now) override;
  void fill_ui(ui::UiState& u) override;
  void on_mode(uint8_t mode, uint32_t now) override;
  bool on_sys(mp::SysCmd cmd, uint32_t now) override;
  bool console(const char* line) override;
  void info() override;
  const char* help() const override { return "enc (knob pins for 5 s)"; }

  bool reversed() const;

 private:
  static void isr();  // both CLK and DT, on every edge

  static QuadDecoder s_quad;
  static volatile int32_t s_steps;  // detents counted by the interrupt, consumed by task()

  mp::Engine* engine_ = nullptr;
  uint8_t* saved_ = nullptr;  // [0] = direction reversed
  uint32_t last_sw_ms_ = 0;
  uint8_t sw_count_ = 0;
  bool sw_ = false;
};
