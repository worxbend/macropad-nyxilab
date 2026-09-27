// Screen layouts, drawn into off-screen canvases.
#pragma once
#include <Adafruit_GFX.h>

#include "ui_state.h"

namespace ui {

void render_main_screen(GFXcanvas16& canvas, const UiState& s);  // 320 x 170
void render_bar_screen(GFXcanvas16& canvas, const UiState& s);   // 76 x 284

}  // namespace ui
