// Renders the real firmware UI (src/ui/render.cpp + src/core/keymap.cpp) to
// PPM files, so layouts can be reviewed without hardware.  Built and run by
// tools/ui_sim/run.py.
#include <stdio.h>

#include "ui/render.h"

using namespace ui;

static void save_ppm(GFXcanvas16& c, const char* path) {
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
  const uint16_t* px = c.getBuffer();
  for (int i = 0; i < c.width() * c.height(); i++) {
    const uint16_t v = px[i];
    const uint8_t rgb[3] = {uint8_t(((v >> 11) & 0x1F) * 255 / 31), uint8_t(((v >> 5) & 0x3F) * 255 / 63),
                            uint8_t((v & 0x1F) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static UiState base_state() {
  UiState s;
  s.usb_mounted = true;
  s.displays_on = true;
  for (int i = 0; i < ACTIVITY_SAMPLES; i++) s.activity[i] = uint8_t((i * 7 + 3) % 5 + (i > 24 ? 4 : 0));
  return s;
}

static void scene(const char* name, const UiState& s, const char* outdir) {
  GFXcanvas16 main_c(320, 170), bar_c(76, 284);
  render_main_screen(main_c, s);
  render_bar_screen(bar_c, s);
  char path[512];
  snprintf(path, sizeof path, "%s/%s_main.ppm", outdir, name);
  save_ppm(main_c, path);
  snprintf(path, sizeof path, "%s/%s_bar.ppm", outdir, name);
  save_ppm(bar_c, path);
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : ".";
  UiState s = base_state();
  scene("01_media_idle", s, out);

  s.keys_down[1][1] = true;  // PLAY pressed
  s.joy_x = 0.55f;
  s.joy_y = 0.35f;
  s.joy_mode = 0;
  scene("02_media_active", s, out);

  s = base_state();
  s.base_layer = s.top_layer = 1;
  s.layer_mask = 1u << 1;
  s.joy_mode = 2;
  s.leds = 0x02;
  s.keys_down[1][1] = true;
  s.joy_y = 0.8f;
  scene("03_edit_caps", s, out);

  s = base_state();
  s.base_layer = 0;
  s.top_layer = 3;
  s.layer_mask = 1u | (1u << 3);
  s.keys_down[3][0] = true;
  scene("04_fn_held", s, out);

  s = base_state();
  s.base_layer = s.top_layer = 2;
  s.layer_mask = 1u << 2;
  s.joy_mode = 1;
  s.joy_button = true;
  s.usb_suspended = true;
  scene("05_fkeys_scroll", s, out);
  printf("rendered 5 scenes to %s\n", out);
  return 0;
}
