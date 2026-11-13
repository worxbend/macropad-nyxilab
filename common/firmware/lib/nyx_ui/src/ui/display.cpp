// Rendering for both displays (and the LED sticks).  Runs on core 1: every frame is drawn
// into an off-screen canvas, and the rows that changed are pushed in one SPI burst, so there
// is no flicker.
#include "display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <SPI.h>
#include <pico/mutex.h>

#include "config.h"
#include "dirty_rows.h"
#include "leds.h"
#include "render.h"

namespace ui {
namespace {

// ------------------------------------------------------------ shared state
auto_init_mutex(g_mtx);
UiState g_shared;
uint32_t g_pub_seq = 0;
uint32_t g_fetch_seq = 0;

// ------------------------------------------------------------------ panels
class Panel : public Adafruit_ST7789 {
 public:
  Panel(SPIClass* spi, int8_t cs, int8_t dc, int8_t rst) : Adafruit_ST7789(spi, cs, dc, rst) {}
  void orient(uint8_t rotation, bool bgr) {
    setRotation(rotation);
    if (!bgr) return;
    static const uint8_t kMadctl[4] = {0xC0, 0xA0, 0x00, 0x60};  // Adafruit's values, RGB
    uint8_t m = kMadctl[rotation & 3] | 0x08;
    sendCommand(ST77XX_MADCTL, &m, 1);
  }
};

Panel g_main(&SPI, cfg::MAIN_CS, cfg::MAIN_DC, cfg::MAIN_RST);
Panel g_bar(&SPI1, cfg::BAR_CS, cfg::BAR_DC, cfg::BAR_RST);
GFXcanvas16* g_cmain = nullptr;
GFXcanvas16* g_cbar = nullptr;
uint32_t* g_main_rows = nullptr;  // row hashes of the frame each panel shows
uint32_t* g_bar_rows = nullptr;
bool g_push_all = true;  // the first frame: the hashes describe nothing yet

uint8_t g_applied_bl = 0;  // what display_begin() wrote to both pins
uint32_t g_last_frame = 0;
bool g_force = true;

// The SPI push blocks core 1 (no LED frames meanwhile), so only the changed band is sent.
void push_changed(Panel& panel, GFXcanvas16& canvas, uint32_t* shown_rows) {
  RowSpan rows = changed_rows(canvas.getBuffer(), canvas.width(), canvas.height(), shown_rows);
  if (g_push_all) rows = {0, canvas.height()};
  if (!rows.count) return;
  panel.drawRGBBitmap(0, rows.first, canvas.getBuffer() + rows.first * canvas.width(), canvas.width(), rows.count);
}

}  // namespace

// ------------------------------------------------------------------ public
void publish(const UiState& s) {
  mutex_enter_blocking(&g_mtx);
  g_shared = s;
  g_pub_seq++;
  mutex_exit(&g_mtx);
}

bool fetch(UiState& out) {
  mutex_enter_blocking(&g_mtx);
  const bool changed = g_pub_seq != g_fetch_seq;
  out = g_shared;
  g_fetch_seq = g_pub_seq;
  mutex_exit(&g_mtx);
  return changed;
}

void display_begin() {
  SPI.setRX(NOPIN);  // write-only buses: do not claim a MISO pin
  SPI.setSCK(cfg::MAIN_SCK);
  SPI.setTX(cfg::MAIN_MOSI);
  SPI1.setRX(NOPIN);
  SPI1.setSCK(cfg::BAR_SCK);
  SPI1.setTX(cfg::BAR_MOSI);

  analogWriteFreq(20000);
  analogWriteRange(255);
  analogWrite(cfg::MAIN_BL, 0);
  analogWrite(cfg::BAR_BL, 0);

  g_main.init(cfg::MAIN_W, cfg::MAIN_H);
  g_main.setSPISpeed(cfg::SPI_HZ);
  g_main.orient(cfg::MAIN_ROTATION, cfg::MAIN_BGR);
  g_main.invertDisplay(cfg::MAIN_INVERT);
  g_main.fillScreen(0);

  g_bar.init(cfg::BAR_W, cfg::BAR_H);
  g_bar.setSPISpeed(cfg::SPI_HZ);
  g_bar.orient(cfg::BAR_ROTATION, cfg::BAR_BGR);
  g_bar.invertDisplay(cfg::BAR_INVERT);
  g_bar.fillScreen(0);

  g_cmain = new GFXcanvas16(g_main.width(), g_main.height());
  g_cbar = new GFXcanvas16(g_bar.width(), g_bar.height());
  g_main_rows = new uint32_t[g_main.height()]();
  g_bar_rows = new uint32_t[g_bar.height()]();
  leds_begin();
}

void display_task(uint32_t now) {
  static UiState s;
  if (fetch(s)) g_force = true;
  leds_task(now, s);
  const uint8_t bl = s.displays_on ? s.brightness : 0;
  if (bl != g_applied_bl) {
    analogWrite(cfg::MAIN_BL, bl);
    analogWrite(cfg::BAR_BL, bl);
    g_applied_bl = bl;
  }
  if (!bl || !g_cmain || !g_cbar) return;
  // draw at most 40 times a second; core 0 publishes every 10 ms, so every frame is due
  const bool due = (g_force && now - g_last_frame >= 25) || now - g_last_frame >= 1000;
  if (!due) return;
  g_force = false;
  g_last_frame = now;
  render_main_screen(*g_cmain, s);
  push_changed(g_main, *g_cmain, g_main_rows);
  render_bar_screen(*g_cbar, s);
  push_changed(g_bar, *g_cbar, g_bar_rows);
  g_push_all = false;
}

}  // namespace ui
