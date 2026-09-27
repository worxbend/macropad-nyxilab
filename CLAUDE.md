# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Git commits and pull requests

Do not add any Claude/AI attribution to commits or PRs in this repo. That means no `Co-Authored-By: Claude ...` trailers, no "Generated with Claude Code" lines, and no other mention of Claude in commit messages or PR descriptions.

## Project overview

Monorepo for a hand-wired macropad: 12 MX keys, KY-023 joystick, a 1.9" 170x320 ST7789 and a
2.25" 76x284 ST7789P3 display, Raspberry Pi Pico 2 (RP2350, USB-C clone; RP2040 Pico also supported).

- `cad/` - parametric 3-layer wedge case in build123d (Python). Generated outputs are committed in `cad/exports/`.
- `firmware/` - PlatformIO project (Arduino-Pico core, Adafruit TinyUSB, Adafruit GFX/ST7789).
- `docs/` - design notes, printing, assembly, wiring; images are generated (renders, UI sim, diagrams).

## Commands

```bash
make setup          # cad/.venv (build123d, manifold3d, trimesh, moderngl) + PlatformIO
make cad            # checks + exports + renders + drawings (~40 s); cd cad && PYTHONPATH=. .venv/bin/python -m macropad_cad <check|export|render|drawings|all>
make cad-check      # 128 interference pairs + clearances + printability; non-zero exit on failure
make firmware       # pio run -e pico2, copies UF2 to firmware/dist/
make firmware-pico  # RP2040 build
make flash          # pio run -e pico2 -t upload
make test           # pio test -e native (19 Unity tests of firmware/src/core)
make ui-sim         # renders the real firmware UI to docs/images/ui/*.png (host g++)
make diagrams       # docs/images/wiring|matrix.{svg,png}, parsed from firmware/include/config.h
```

FreeCAD export runs only when `freecadcmd` is on PATH or `FREECAD_CMD` is set. Renders need EGL (GPU or Mesa).

## Architecture

### CAD (`cad/macropad_cad`)
`params.py` (every dimension; `MEASURE` = from photos, verify) -> `layout.py` (derived positions; plate frame
`u,v,w` on the tilted top surface, world X right / Y back / Z up) -> `components.py` (reference models +
keep-out volumes + joystick knob sweep) -> `parts.py` (`top_plate`, `frame`, `base`, `bar_spine`, all in world
coordinates; `print_pose` orients them). `checks.py` must stay green after any change. Key design facts:
screw bosses belong to the top plate and are world-vertical (all 8 case screws M3x8); the frame is the
compression member; the joystick stands on posts from the base (plate bosses would hit the dome sweep); the
Pico clone has no USB-end holes, so the frame's thin USB wall captures the receptacle.

### Firmware (`firmware/`)
- `src/core/` is pure C++ (no Arduino) and unit-tested natively with its own keymap in `test/test_core`:
  `keycodes.h` (32-bit Action = type:8|arg:24), `keymap.cpp` (user layers), `engine.cpp` (layers, tap-hold
  with hold-on-other-key, macros -> `HidState`), `joystick.cpp`, `debounce.h`.
- `src/hw/`: `matrix` (COL2ROW scan), `usb_hid` (one report per USB frame, mouse deltas accumulated),
  `settings` (EEPROM emulation, lazy writes).
- `src/ui/`: `render.cpp` draws into GFX canvases (also compiled by `tools/ui_sim`), `display.cpp` owns the
  panels (SPI0 = 1.9", SPI1 = 2.25", `setRX(NOPIN)`), `ui_state.h` hands a snapshot from core 0 to core 1.
- `src/main.cpp`: core 0 = scan/engine/joystick/USB/console, core 1 = `display_task`.
- Pin map is only in `include/config.h`; `docs/tools/wiring_diagram.py` parses it - keep docs/wiring.md in sync.
- Note: the core defines `USB_MANUFACTURER`/`USB_PRODUCT` macros; don't reuse those names.
