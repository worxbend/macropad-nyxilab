// Both ST7789 displays: init + rendering (runs on core 1).
#pragma once
#include <stdint.h>

#include "ui_state.h"

namespace ui {

void display_begin();
void display_task(uint32_t now_ms);

}  // namespace ui
