// Renders the real firmware UI (ui/render.cpp + the edition's keymap.cpp) and
// the LED effects (core/led_fx.cpp) to files, so layouts can be reviewed without
// hardware.  Built and run by run.py, once per edition; the scenes come from
// <edition>/firmware/tools/ui_scenes.cpp.
#include "ui_sim.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "core/keymap.h"

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

// the 16 LED values as the firmware would send them, one "r g b" line each
static void save_leds(const UiState& s, uint32_t since_hit, const char* path) {
  mp::LedInput in;
  in.mode = mp::LedMode(s.led_mode);
  in.color565 = mp::LAYERS[s.top_layer].color;
  in.level = s.usb_suspended ? 0 : s.brightness;  // the firmware blanks everything while the host sleeps
  in.max_level = cfg::LED_MAX_BRIGHTNESS;
  in.budget_ma = cfg::LED_BUDGET_MA;
  in.since_hit_ms = since_hit;
  mp::Rgb px[cfg::LED_COUNT];
  const uint32_t now = 1000;  // same frame for every scene
  mp::led_frame(in, now, px, cfg::LED_COUNT);
  FILE* f = fopen(path, "w");
  fprintf(f, "%s\n", mp::led_mode_name(in.mode));
  for (const auto& p : px) fprintf(f, "%u %u %u\n", p.r, p.g, p.b);
  fclose(f);
}

UiState base_state() {
  UiState s;
  s.usb_mounted = true;
  s.displays_on = true;
  s.enc_label = mp::LAYERS[0].centre.knob.label;
  for (int i = 0; i < ui::ACTIVITY_SAMPLES; i++) s.activity[i] = uint8_t((i * 7 + 3) % 5 + (i > 24 ? 4 : 0));
  return s;
}

void set_layer(UiState& s, uint8_t base, uint8_t top) {
  s.base_layer = base;
  s.top_layer = top;
  s.layer_mask = (1u << base) | (1u << top);
  s.enc_label = mp::LAYERS[top].centre.knob.label;
  s.view = uint8_t(mp::LAYERS[top].view);
}

uint8_t host_layer() {
  for (uint8_t l = 0; l < mp::NUM_LAYERS; l++)
    if (mp::LAYERS[l].view == mp::View::Host) return l;
  return 0;
}

void host_demo(UiState& s) {
  mp::HostData& d = s.host;
  d.linked = d.has_metrics = true;
  strcpy(d.hostname, "workstation");
  mp::HostMetrics& m = d.m;
  m.cpu_pct = 37;
  m.n_cores = 16;
  const uint8_t cores[16] = {62, 18, 41, 9, 88, 23, 35, 12, 71, 30, 15, 47, 26, 93, 8, 20};
  memcpy(m.core_pct, cores, sizeof cores);
  m.mem_used_mib = 11674;
  m.mem_total_mib = 31890;
  m.swap_pct = 3;
  m.disk_pct = 61;
  m.cpu_temp_c10 = 547;
  m.load1_x100 = 214;
  m.net_rx_bps = 1840000;
  m.net_tx_bps = 96300;
  m.uptime_s = 3 * 86400 + 5 * 3600 + 120;
  m.gpu_pct = 12;
  m.gpu_temp_c10 = 461;
  m.clock_h = 14;
  m.clock_m = 7;
  for (int i = 0; i < mp::HOST_HISTORY; i++) {
    d.cpu_hist[i] = uint8_t(20 + (i * 37 % 23) + (i > 26 ? 18 : 0));
    d.mem_hist[i] = uint8_t(34 + i / 12);
  }
  strcpy(d.text[0], "build #482 passed");
  strcpy(d.text[1], "3 unread mails");
}

void scene(const char* name, const UiState& s, const char* outdir, uint32_t since_hit) {
  GFXcanvas16 main_c(320, 170), bar_c(76, 284);
  ui::render_main_screen(main_c, s);
  ui::render_bar_screen(bar_c, s);
  char path[512];
  snprintf(path, sizeof path, "%s/%s_main.ppm", outdir, name);
  save_ppm(main_c, path);
  snprintf(path, sizeof path, "%s/%s_bar.ppm", outdir, name);
  save_ppm(bar_c, path);
  snprintf(path, sizeof path, "%s/%s_leds.txt", outdir, name);
  save_leds(s, since_hit, path);
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : ".";
  edition_scenes(out);
  printf("rendered scenes to %s\n", out);
  return 0;
}
