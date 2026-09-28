// The shared application.
//
// core 0: matrix scan -> debounce -> engine -> USB HID, the centre control, settings,
//         serial console, UI snapshots
// core 1: both displays (rendering + SPI transfers) and the RGB LED sticks
//
// The edition plugs in its centre control (the thumbstick or the knob) through the
// Centre interface; its main.cpp is a few lines that hand one to app::begin().
#pragma once
#include <stdint.h>

#include "core/engine.h"
#include "ui/ui_state.h"

namespace app {

class Centre {
 public:
  virtual ~Centre() = default;
  // `saved`: 16 bytes of the flash settings that belong to the centre control (all zero on first boot).
  virtual void begin(mp::Engine& engine, uint8_t* saved) = 0;
  virtual void task(uint32_t now) = 0;  // every loop iteration, after engine.tick()
  virtual void fill_ui(ui::UiState& u) = 0;
  virtual void on_mode(uint8_t mode, uint32_t now) = 0;  // a JOY() / KNOB_PRESET() / *_CYCLE key was pressed
  virtual bool on_sys(mp::SysCmd cmd, uint32_t now) {  // true = the command was the centre control's
    (void)cmd;
    (void)now;
    return false;
  }
  virtual bool console(const char* line) {  // true = the command was handled
    (void)line;
    return false;
  }
  virtual void info() {}                 // extra lines for the `info` console command
  virtual const char* help() const = 0;  // its console commands, e.g. "joy (raw stick for 5 s) | cal"
};

// Services for the centre control.
mp::Engine& engine();
void activity(uint32_t now);          // resets the idle timers (backlight dimming / sleep)
void hit(uint32_t now);               // a "press" the LEDs flash on
void settings_changed(uint32_t now);  // save the settings (incl. the centre's bytes) in a few seconds

// Arduino entry points: call these from setup() / loop() / setup1() / loop1().
void begin(Centre& centre);
void loop();
void begin1();
void loop1();

}  // namespace app
