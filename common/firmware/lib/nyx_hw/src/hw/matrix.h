// Key matrix scanning (hand-wired, COL2ROW diodes).
#pragma once
#include <stdint.h>

#include "core/keymap.h"

namespace hw {

void matrix_init();
void matrix_scan(uint8_t (&raw)[mp::ROWS]);
bool matrix_boot_combo_held();

}  // namespace hw
