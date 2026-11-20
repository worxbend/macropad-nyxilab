// UI simulator scenes of the rotary edition (built by tools/ui_sim/run.py).
#include <string.h>

#include "core/themes.h"
#include "presets.h"
#include "ui_sim.h"

// what KnobCentre::fill_ui() sets on the device: draw the knob dial, not the stick
static UiState knob_state() {
  UiState s = base_state();
  s.centre_encoder = true;
  return s;
}

void edition_scenes(const char* out) {
  UiState s = knob_state();
  scene("01_media_idle", s, out);

  s.enc_pos = 3;  // turned up three detents: volume up
  scene("02_media_volume", s, out, 60);

  s = knob_state();
  set_layer(s, 1, 1);
  s.leds = 0x02;
  s.enc_pos = -2;
  s.enc_press = true;
  scene("03_edit_history", s, out);

  s = knob_state();
  set_layer(s, 0, mp::FN_LAYER);
  s.keys_down[3][0] = true;
  s.enc_pos = 7;
  scene("04_fn_bright", s, out);

  s = knob_state();
  set_layer(s, 0, 0);
  s.enc_label = mp::ENC_PRESETS[2].label;  // FN + KNOB picked the ZOOM preset
  s.led_mode = uint8_t(mp::LedMode::Rainbow);
  s.enc_pos = 12;
  scene("05_media_zoom_rainbow", s, out);

  s = knob_state();
  set_layer(s, 3, 3);  // F9-16, the second F-key page
  s.keys_down[2][0] = true;  // NEXT
  scene("08_fkeys_page2", s, out);

  s = knob_state();
  set_layer(s, host_layer(), host_layer());
  host_demo(s);
  scene("06_host_dashboard", s, out);

  s = knob_state();
  set_layer(s, host_layer(), host_layer());
  scene("07_host_waiting", s, out);

  s = knob_state();
  set_layer(s, view_layer(mp::View::Sway), view_layer(mp::View::Sway));
  host_demo(s);
  sway_demo(s);
  scene("09_host_sway", s, out);

  s = knob_state();
  set_layer(s, view_layer(mp::View::Sway), view_layer(mp::View::Sway));
  host_demo(s);
  scene("10_host_sway_off", s, out);

  s = knob_state();
  set_layer(s, view_layer(mp::View::Player), view_layer(mp::View::Player));
  host_demo(s);
  player_demo(s);
  scene("11_host_player", s, out);

  s.host.player.status = mp::PLAYER_PAUSED;
  s.host.player.volume = mp::HOST_UNKNOWN;
  s.host.player.position_s = 1999;
  s.host.player.length_s = 5400;
  strcpy(s.host.player.name, "firefox");
  strcpy(s.host.player.title, "Lecture 7: Signals");
  strcpy(s.host.player.artist, "");
  strcpy(s.host.player.album, "");
  scene("12_host_player_paused", s, out);

  s = knob_state();
  set_layer(s, view_layer(mp::View::Player), view_layer(mp::View::Player));
  host_demo(s);
  scene("13_host_no_player", s, out);

  // CONFIG: every key shows the setting it changes
  s = knob_state();
  set_layer(s, view_layer(mp::View::Config), view_layer(mp::View::Config));
  s.bar_theme = mp::THEME_MIDNIGHT;
  s.led_color = 5;  // PURPLE
  s.led_level = 6;
  s.led_mode = uint8_t(mp::LedMode::Breathe);
  s.keys_down[1][1] = true;  // LEDCOL
  scene("14_config", s, out, 60);

  // the themes: each display has its own
  s = knob_state();
  s.main_theme = mp::THEME_LIGHT;
  s.bar_theme = mp::THEME_MIDNIGHT;
  s.keys_down[1][1] = true;
  s.leds = 0x02;
  scene("15_media_light", s, out, 60);

  s = knob_state();
  set_layer(s, host_layer(), host_layer());
  host_demo(s);
  s.main_theme = mp::THEME_FOREST;
  scene("16_host_sys_forest", s, out);

  s = knob_state();
  set_layer(s, view_layer(mp::View::Sway), view_layer(mp::View::Sway));
  host_demo(s);
  sway_demo(s);
  s.main_theme = mp::THEME_LIGHT;
  scene("17_host_sway_light", s, out);

  s = knob_state();
  set_layer(s, view_layer(mp::View::Player), view_layer(mp::View::Player));
  host_demo(s);
  player_demo(s);
  s.main_theme = mp::THEME_AMBER;
  scene("18_host_player_amber", s, out);

  s = knob_state();
  set_layer(s, 0, mp::FN_LAYER);
  s.keys_down[3][0] = true;
  s.main_theme = mp::THEME_MONO;
  scene("19_fn_mono", s, out);
}
