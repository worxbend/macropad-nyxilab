// UI simulator scenes of the joystick edition (built by tools/ui_sim/run.py).
#include "ui_sim.h"

void edition_scenes(const char* out) {
  UiState s = base_state();
  scene("01_media_idle", s, out);

  s.keys_down[1][1] = true;  // PLAY pressed
  s.joy_x = 0.55f;
  s.joy_y = 0.35f;
  s.joy_mode = 0;
  scene("02_media_active", s, out, 60);

  s = base_state();
  set_layer(s, 1, 1);
  s.joy_mode = 2;
  s.leds = 0x02;
  s.keys_down[1][1] = true;
  s.joy_y = 0.8f;
  scene("03_edit_caps", s, out, 150);

  s = base_state();
  set_layer(s, 0, 3);
  s.keys_down[3][0] = true;
  scene("04_fn_held", s, out);

  s = base_state();
  set_layer(s, 2, 2);
  s.joy_mode = 1;
  s.joy_button = true;
  s.usb_suspended = true;
  s.led_mode = uint8_t(mp::LedMode::Rainbow);
  scene("05_fkeys_scroll", s, out);
}
