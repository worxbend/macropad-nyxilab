# CAD

The parametric case, in [build123d](https://github.com/gumyr/build123d) (OpenCASCADE
B-rep), with mesh checks in [manifold3d](https://github.com/elalish/manifold), headless
renders (moderngl/EGL) and matplotlib blueprints.

This package (`macropad_cad`) is the **shared** model: the tilted top plate with the
switches and the two displays, the wedge frame with the USB-C port, the base with the
Pico cradle and the LED-stick posts, the bar-display spine, the fit checks, the
exporters, the renders and the drawings. It does not know what sits in the middle.
Each edition adds that in its own project:

| Edition | `edition.py` | Outputs | Images |
|---|---|---|---|
| [`joystick/cad`](../../joystick/cad) | KY-023 thumbstick: posts in the base, bevelled opening in the plate, dome sweep | `joystick/cad/exports/` | `docs/images/joystick/` |
| [`rotary/cad`](../../rotary/cad) | KY-040 encoder: panel mount in the plate, printed knob, knob envelope | `rotary/cad/exports/` | `docs/images/rotary/` |

Frame and bar spine come out identical in both editions.

## Setup and commands

```bash
python3 -m venv .venv && .venv/bin/pip install -e "common/cad[render]"   # or `make setup` from the repo root
.venv/bin/python joystick/cad/build.py check      # fit / interference / printability checks
.venv/bin/python joystick/cad/build.py export     # STEP, STL, 3MF, GLB (+ FreeCAD)
.venv/bin/python joystick/cad/build.py render     # renders into docs/images/joystick
.venv/bin/python joystick/cad/build.py drawings   # blueprints into joystick/cad/exports/drawings
.venv/bin/python joystick/cad/build.py all        # everything (~2.5 min with FreeCAD)
```

Same for `rotary/cad/build.py`. From the repo root: `make cad` (both editions),
`make cad-joystick CMD=check`, `make cad-rotary`.

The FreeCAD document needs FreeCAD 1.x: put `freecadcmd` on the PATH or set
`FREECAD_CMD` (an extracted AppImage works: `FreeCAD.AppImage --appimage-extract`,
then `squashfs-root/usr/bin/freecadcmd`). Without it the previous document is kept.
Rendering needs an OpenGL driver (EGL); everything else is pure Python.

## Changing the design

Shared dimensions live in `macropad_cad/params.py` (components, fasteners,
tolerances, the case), grouped by part; the centre control's dimensions live in the
edition's `edition.py`. Values marked `MEASURE` should be checked against your own
modules. Typical edits:

| Want | Change |
|---|---|
| steeper / flatter wedge | `Case.tilt_deg` (front height is set by `Case.front_h`) |
| tighter or looser switch fit | `MXSwitch.hole` |
| modules loose in their pockets | `PrintTol.pocket` |
| different insert size / self-tapping | `Fasteners.insert_d`, `insert_depth` |
| no logo | `Case.logo = ""` |
| move the centre control / displays | `Case.centre_v`, `Case.bar_v`, `Case.center_half` |
| move the LED sticks | `Case.led_dx` (from the Pico axis), `Case.led_y` |
| LED stick with other holes | `LedStick.hole_pitch`, `hole_y`, `hole_d` |
| joystick opening, posts, relief | `JoystickMount` in `joystick/cad/edition.py` |
| knob too tight / loose on the shaft, knob size, grip | `Knob.bore_clear`, `Knob.d`, `h`, `flutes` in `rotary/cad/edition.py` |
| encoder clamp, pocket, ring | `EncoderMount` in `rotary/cad/edition.py` |

Run `check` after every change: it fails (non-zero exit) if any printed part intrudes
into a component, its wiring space, the joystick sweep or the knob's envelope, or if
parts or components overlap. The numbers also go to `<edition>/cad/exports/checks.json`.

## Package layout

```
params.py      shared dimensions (components, fasteners, tolerances, case)
edition.py     the Edition interface: the hooks an edition implements (geometry, checks, drawings)
layout.py      derived positions: key grid, module placements, bosses, heights; Layout(edition)
components.py  reference models of switches, keycaps, Pico 2, displays, LED sticks (+ keep-outs)
parts.py       top_plate, frame, base, bar_spine; print orientation (the edition adds its parts)
fit_tests.py   quick coupons for the critical fits (+ helpers for the editions' coupons)
checks.py      interference, clearance and printability checks (manifold3d, trimesh)
export.py      STEP / STL / 3MF / GLB writers, FreeCAD hand-off
freecad_doc.py runs inside FreeCAD to build the .FCStd
render.py      headless OpenGL renderer; renders.py = the standard views
drawings.py    A3 blueprint sheets, section view, 1:1 template
cli.py         the build.py command line
```

## Outputs (`<edition>/cad/exports/`)

| Folder | Content |
|---|---|
| `stl/`, `3mf/` | print-ready parts in print orientation; `fit-tests/` coupons |
| `step/` | each part in assembled position (editable in any CAD), full colour assembly, reference components |
| `freecad/` | `nyxilab_macropad_<edition>.FCStd`: printed parts, reference components, a layout spreadsheet |
| `gltf/` | coloured assembly / case for viewers (`docs/viewer.html` embeds both editions) |
| `drawings/` | `nyxilab_macropad_<edition>_blueprints.pdf` (5 × A3), per-sheet SVG, `template_top_plate_1to1.pdf` |
| `manifest.json` | layout summary, part sizes/volumes, print notes |
| `checks.json` | fit-check count, clearances, printability report, facts for the viewer |
