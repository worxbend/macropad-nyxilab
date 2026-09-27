// ============================================================================
//  Hardware configuration - pins match docs/wiring.md
//
//  Raspberry Pi Pico 2 (RP2350) or Pico (RP2040), USB-C clone boards.
//  "GPx" numbers below are GPIO numbers, not physical pin numbers.
// ============================================================================
#pragma once
#include <stdint.h>

#include "core/keymap.h"

namespace cfg {

// ---- key matrix, COL2ROW: diode cathode (black band) towards the ROW wire --
constexpr uint8_t ROW_PINS[mp::ROWS] = {2, 3, 4, 5};  // R0 = back row ... R3 = front row
constexpr uint8_t COL_PINS[mp::COLS] = {6, 7, 8};     // C0 = left column, C1/C2 = right block
constexpr uint8_t DEBOUNCE_MS = 5;
constexpr uint32_t SCAN_INTERVAL_US = 1000;

// ---- KY-023 joystick.  Power it from 3V3 (pin 36), NOT 5 V: the ADC is 3.3 V max
constexpr uint8_t JOY_X_PIN = 26;   // ADC0  (module pin VRx)
constexpr uint8_t JOY_Y_PIN = 27;   // ADC1  (module pin VRy)
constexpr uint8_t JOY_SW_PIN = 22;  // module pin SW, internal pull-up
constexpr bool JOY_INVERT_X = false;  // flip if the pointer moves the wrong way
constexpr bool JOY_INVERT_Y = false;
constexpr bool JOY_SWAP_XY = false;
constexpr uint32_t JOY_PERIOD_MS = 8;

// ---- main display: 1.9" 170x320 ST7789 on SPI0 ---------------------------------
constexpr uint8_t MAIN_SCK = 18, MAIN_MOSI = 19, MAIN_CS = 17, MAIN_DC = 16, MAIN_RST = 20, MAIN_BL = 21;
constexpr uint16_t MAIN_W = 170, MAIN_H = 320;  // native panel size
constexpr uint8_t MAIN_ROTATION = 1;            // landscape; use 3 if the image is upside down
constexpr bool MAIN_INVERT = true;              // IPS panels need colour inversion
constexpr bool MAIN_BGR = false;                // set if red and blue are swapped

// ---- bar display: 2.25" 76x284 ST7789P3 on SPI1 --------------------------------
constexpr uint8_t BAR_SCK = 10, BAR_MOSI = 11, BAR_CS = 13, BAR_DC = 12, BAR_RST = 14, BAR_BL = 15;
constexpr uint16_t BAR_W = 76, BAR_H = 284;
constexpr uint8_t BAR_ROTATION = 2;  // portrait, header towards the user; use 0 if upside down
constexpr bool BAR_INVERT = true;
constexpr bool BAR_BGR = false;

constexpr uint32_t SPI_HZ = 40000000;  // safe for hand-wired leads; 62.5 MHz = faster refresh, 20 MHz if glitchy

// ---- backlight & idle ------------------------------------------------------------
constexpr uint8_t BRIGHTNESS_DEFAULT = 200;  // 0..255
constexpr uint8_t BRIGHTNESS_STEP = 32;
constexpr uint32_t DIM_AFTER_MS = 60UL * 1000;       // dim after 1 min idle
constexpr uint32_t SLEEP_AFTER_MS = 10UL * 60 * 1000;  // backlight off after 10 min
constexpr uint8_t BRIGHTNESS_DIMMED = 24;

// ---- USB --------------------------------------------------------------------------
constexpr const char* USB_VENDOR_NAME = "Nyxilab";
constexpr const char* USB_PRODUCT_NAME = "Nyxilab Macropad";

}  // namespace cfg
