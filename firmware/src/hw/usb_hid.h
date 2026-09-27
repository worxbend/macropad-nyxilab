// Composite USB HID (keyboard + mouse + consumer) on Adafruit TinyUSB.
//
// Reports share one interrupt endpoint, so changes are queued and sent one
// per USB frame when the endpoint is free; mouse motion is accumulated so no
// movement is lost while the endpoint is busy.
#pragma once
#include <stdint.h>

#include "core/engine.h"

namespace hw {

void usb_begin();
void usb_task();  // call every loop iteration

void usb_set_keyboard(const mp::HidState& s);  // keys + modifiers + consumer + buttons
void usb_add_mouse(int8_t dx, int8_t dy, int8_t wheel, int8_t pan);
void usb_set_extra_buttons(uint8_t buttons);  // e.g. joystick click

bool usb_mounted();
bool usb_suspended();
uint8_t usb_leds();  // host LED state: bit0 NUM, bit1 CAPS, bit2 SCROLL

}  // namespace hw
