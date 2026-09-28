# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Git commits and pull requests

Do not add any Claude/AI attribution to commits or PRs in this repo. That means no `Co-Authored-By: Claude ...` trailers, no "Generated with Claude Code" lines, and no other mention of Claude in commit messages or PR descriptions.

## Project overview

Monorepo for a hand-wired macropad: 12 MX keys, a 1.9" 170x320 ST7789 and a 2.25" 76x284 ST7789P3 display,
two 8 x WS2812B LED sticks on the base, Raspberry Pi Pico 2 (RP2350, USB-C clone; RP2040 Pico also supported).
Two editions differ only in the centre control: **joystick** (KY-023) and **rotary** (KY-040 encoder + printed knob).
Four projects plus the shared code:

- `joystick/cad`, `rotary/cad` - one `edition.py` each (the centre control's geometry, checks, drawing notes), a
  `build.py`, and the committed outputs in `exports/` (STL, 3MF, STEP, FreeCAD, GLB, blueprints, manifest, checks).
- `joystick/firmware`, `rotary/firmware` - PlatformIO projects: `include/config.h` (pins), `src/keymap.cpp`, the
  centre control (`stick.cpp` / `knob.cpp` implementing `app::Centre`), `main.cpp` (4 lines), `test/`, `dist/` (UF2s).
- `common/cad/macropad_cad` - the shared case model (build123d), which calls the edition through `Edition` hooks.
- `common/firmware/lib` - the shared firmware as PlatformIO libraries: `nyx_core` (engine, keymap types, LED effects;
  pure C++, tests in `common/firmware/test`), `nyx_hw` (matrix, USB HID, settings), `nyx_ui` (renderer, panels, LEDs),
  `nyx_app` (the application loop; `app::Centre` is the edition interface).
- `docs/` - guides (design, printing, soldering, wiring, assembly), generated images in `docs/images/<edition>/` and
  `docs/images/` (wiring diagrams, wiring maps, soldering figures), `docs/viewer.html` (both editions).
- `tools/` - `ui_sim/`, `wiring_diagram.py`, `wiring_map.py`, `soldering_figures.py`, `build_viewer.py`,
  `build_site.py` (the website; `.github/workflows/pages.yml` deploys `site/` to GitHub Pages on push to main).

## Commands

```bash
make setup              # .venv (build123d, manifold3d, trimesh, moderngl, markdown) + PlatformIO
make cad                # both editions: checks + exports + renders + drawings  (= python <ed>/cad/build.py all)
make cad-joystick CMD=check   # one edition, one step (check | export | render | drawings | all); cad-rotary likewise
make cad-check          # 169 (joystick) / 191 (rotary) interference pairs + clearances + printability; non-zero exit on failure
make firmware           # both editions for the Pico 2 -> <ed>/firmware/dist/nyxilab-macropad-<ed>-pico2.uf2
make firmware-all       # + the RP2040 builds; firmware-joystick, firmware-rotary(-pico), flash-joystick, flash-rotary
make test               # pio test -e native in common/firmware (23), joystick/firmware (6), rotary/firmware (4)
make docs               # ui-sim + diagrams (wiring, wiring maps, soldering figures) + viewer + site
make site               # site/ for GitHub Pages; python3 -m http.server -d site to look at it
```

FreeCAD export runs only when `freecadcmd` is on PATH or `FREECAD_CMD` is set (otherwise the previous document
is kept). Renders need EGL (GPU or Mesa).

## Architecture

### CAD
`common/cad/macropad_cad`: `params.py` (shared dimensions; `MEASURE` = from photos, verify) -> `layout.py`
(`Layout(edition)`: derived positions; plate frame `u,v,w` on the tilted top surface, world X right / Y back / Z up)
-> `components.py` (reference models + keep-outs) -> `parts.py` (`top_plate`, `frame`, `base`, `bar_spine` in world
coordinates; `print_pose` orients them). `edition.py` defines the `Edition` hooks: `plate_cuts`, `base_features`,
`parts`, `components`, `keepouts`, `sweep`, `fit_tests`, `clearances`, drawing notes. `checks.py` must stay green
for both editions after any change; `<ed>/cad/exports/checks.json` and `manifest.json` feed the viewer.
Key design facts: screw bosses belong to the top plate and are world-vertical (all 8 case screws M3x8); the frame
is the compression member; the joystick stands on posts from the base (plate bosses would hit the dome sweep); the
KY-040 is panel-mounted (M7 nut on a 2 mm clamp section, square key pocket from below); the knob's bore ends at the
shaft tip so presses reach the switch; the LED sticks sit on 3 mm posts either side of the Pico; the Pico clone has
no USB-end holes, so the frame's thin USB wall captures the receptacle. Keep the joystick edition's geometry
unchanged unless asked (its STLs are the regression reference).

### Firmware
- Actions are 32-bit (`type:8 | arg:24`, `keycodes.h`). A layer is `{name, colour, CentreDef, keys}`; `CentreDef` is
  `STICK(mode)` or `KNOB(cw, ccw, press, label)`. `ActType::Centre` actions (`JOY()`, `KNOB_PRESET()`, `KNOB_CYCLE`)
  go to `EngineHost::on_centre`, which `nyx_app` forwards to the edition's `Centre::on_mode`.
- `engine.cpp`: layers, tap-hold with hold-on-other-key, macros, knob binding resolution (`encoder_binding`), queued
  taps, wheel steps -> `HidState`. Presets are injected with `set_encoder_presets` (rotary only).
- `nyx_app/app.cpp`: core 0 = scan/debounce/engine/`Centre::task`/USB/console/UI snapshot, core 1 = displays + LEDs.
  `Settings.centre_modes[]` = stick mode or knob preset per base layer; `Settings.centre[16]` = the Centre's bytes
  (joystick calibration / knob direction).
- The project's `include/config.h` is on the include path of the libraries (`-I${PROJECT_DIR}/include`). Pin maps are
  only there; `tools/wiring_diagram.py` parses both and asserts the shared pins agree - keep `docs/wiring.md` in sync.
- The core defines `USB_MANUFACTURER`/`USB_PRODUCT` macros; don't reuse those names.
