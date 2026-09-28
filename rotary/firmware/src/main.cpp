// Nyxilab macropad firmware, ROTARY edition.
//
// Everything shared (matrix, engine, USB HID, displays, LEDs, console) lives in
// common/firmware/lib; this project adds the KY-040 knob and its keymap.
#include <Arduino.h>

#include "app/app.h"
#include "knob.h"

static KnobCentre g_knob;

void setup() { app::begin(g_knob); }
void loop() { app::loop(); }
void setup1() { app::begin1(); }
void loop1() { app::loop1(); }
