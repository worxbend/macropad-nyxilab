// Helpers for the edition's ui_scenes.cpp (see run.py).
#pragma once
#include "core/led_fx.h"
#include "ui/render.h"

using ui::UiState;

UiState base_state();                                        // USB mounted, displays on, some activity history
void set_layer(UiState& s, uint8_t base, uint8_t top);      // base layer + the layer on top (mp::FN_LAYER)
uint8_t host_layer();                                        // the first layer with View::Host
void host_demo(UiState& s);                                  // a connected daemon with plausible metrics
void scene(const char* name, const UiState& s, const char* outdir, uint32_t since_hit_ms = 0xFFFFFFFFu);

// Defined by <edition>/firmware/tools/ui_scenes.cpp.
void edition_scenes(const char* outdir);
