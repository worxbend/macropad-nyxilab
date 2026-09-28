// RGB LED sticks on the base.  Runs on core 1 next to the displays and shows
// the same UI snapshot, so the LEDs dim and sleep together with the screens.
#pragma once
#include <stdint.h>

#include "ui_state.h"

namespace ui {

void leds_begin();
void leds_task(uint32_t now, const UiState& s);

}  // namespace ui
