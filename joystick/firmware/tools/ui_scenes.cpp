// UI simulator scenes of the joystick edition (built by tools/ui_sim/run.py).
#include <string.h>

#include "core/themes.h"
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
  set_layer(s, 0, mp::FN_LAYER);
  s.keys_down[3][0] = true;
  scene("04_fn_held", s, out);

  s = base_state();
  set_layer(s, 2, 2);
  s.joy_mode = 1;
  s.joy_button = true;
  s.usb_suspended = true;
  s.led_mode = uint8_t(mp::LedMode::Rainbow);
  scene("05_fkeys_scroll", s, out);

  s = base_state();
  set_layer(s, host_layer(), host_layer());
  host_demo(s);
  scene("06_host_dashboard", s, out);

  s = base_state();
  set_layer(s, host_layer(), host_layer());
  scene("07_host_waiting", s, out);

  s = base_state();
  set_layer(s, view_layer(mp::View::Sway), view_layer(mp::View::Sway));
  host_demo(s);
  sway_demo(s);
  scene("09_host_sway", s, out);

  s = base_state();
  set_layer(s, view_layer(mp::View::Sway), view_layer(mp::View::Sway));
  host_demo(s);
  scene("10_host_sway_off", s, out);

  s = base_state();
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

  s = base_state();
  set_layer(s, view_layer(mp::View::Player), view_layer(mp::View::Player));
  host_demo(s);
  scene("13_host_no_player", s, out);

  // CONFIG: every key shows the setting it changes
  s = base_state();
  set_layer(s, view_layer(mp::View::Config), view_layer(mp::View::Config));
  s.bar_theme = mp::THEME_MIDNIGHT;
  s.led_color = 5;  // PURPLE
  s.led_level = 6;
  s.led_mode = uint8_t(mp::LedMode::Breathe);
  s.keys_down[1][1] = true;  // LEDCOL
  scene("14_config", s, out, 60);

  // the themes: each display has its own
  s = base_state();
  s.main_theme = mp::THEME_LIGHT;
  s.bar_theme = mp::THEME_MIDNIGHT;
  s.keys_down[1][1] = true;
  s.leds = 0x02;
  scene("15_media_light", s, out, 60);

  s = base_state();
  set_layer(s, host_layer(), host_layer());
  host_demo(s);
  s.main_theme = mp::THEME_FOREST;
  scene("16_host_sys_forest", s, out);

  s = base_state();
  set_layer(s, view_layer(mp::View::Sway), view_layer(mp::View::Sway));
  host_demo(s);
  sway_demo(s);
  s.main_theme = mp::THEME_LIGHT;
  scene("17_host_sway_light", s, out);

  s = base_state();
  set_layer(s, view_layer(mp::View::Player), view_layer(mp::View::Player));
  host_demo(s);
  player_demo(s);
  s.main_theme = mp::THEME_AMBER;
  scene("18_host_player_amber", s, out);

  s = base_state();
  set_layer(s, 0, mp::FN_LAYER);
  s.keys_down[3][0] = true;
  s.main_theme = mp::THEME_MONO;
  scene("19_fn_mono", s, out);
}
