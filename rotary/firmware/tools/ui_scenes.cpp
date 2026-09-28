// UI simulator scenes of the rotary edition (built by tools/ui_sim/run.py).
#include "presets.h"
#include "ui_sim.h"

void edition_scenes(const char* out) {
  UiState s = base_state();
  scene("01_media_idle", s, out);

  s.enc_pos = 3;  // turned up three detents: volume up
  scene("02_media_volume", s, out, 60);

  s = base_state();
  set_layer(s, 1, 1);
  s.leds = 0x02;
  s.enc_pos = -2;
  s.enc_press = true;
  scene("03_edit_history", s, out);

  s = base_state();
  set_layer(s, 0, 3);
  s.keys_down[3][0] = true;
  s.enc_pos = 7;
  scene("04_fn_bright", s, out);

  s = base_state();
  set_layer(s, 0, 0);
  s.enc_label = mp::ENC_PRESETS[2].label;  // FN + KNOB picked the ZOOM preset
  s.led_mode = uint8_t(mp::LedMode::Rainbow);
  s.enc_pos = 12;
  scene("05_media_zoom_rainbow", s, out);
}
