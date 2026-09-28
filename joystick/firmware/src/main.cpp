// Nyxilab macropad firmware, JOYSTICK edition.
//
// Everything shared (matrix, engine, USB HID, displays, LEDs, console) lives in
// common/firmware/lib; this project adds the KY-023 thumbstick and its keymap.
#include <Arduino.h>

#include "app/app.h"
#include "stick.h"

static JoystickCentre g_stick;

void setup() { app::begin(g_stick); }
void loop() { app::loop(); }
void setup1() { app::begin1(); }
void loop1() { app::loop1(); }
