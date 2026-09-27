# CAD

Parametric model of the case in [build123d](https://github.com/gumyr/build123d)
(OpenCASCADE B-rep), with mesh checks in [manifold3d](https://github.com/elalish/manifold),
headless renders (moderngl/EGL) and matplotlib blueprints.

## Setup and commands

```bash
python3 -m venv .venv && .venv/bin/pip install -e ".[render]"   # or `make setup` from the repo root
PYTHONPATH=. .venv/bin/python -m macropad_cad check      # fit / interference / printability checks
PYTHONPATH=. .venv/bin/python -m macropad_cad export     # STEP, STL, 3MF, GLB (+ FreeCAD)
PYTHONPATH=. .venv/bin/python -m macropad_cad render     # renders into ../docs/images
PYTHONPATH=. .venv/bin/python -m macropad_cad drawings   # blueprints into exports/drawings
PYTHONPATH=. .venv/bin/python -m macropad_cad all        # everything (~40 s)
```

The FreeCAD document needs FreeCAD 1.x: put `freecadcmd` on the PATH or set
`FREECAD_CMD` (an extracted AppImage works: `FreeCAD.AppImage --appimage-extract`,
then `squashfs-root/usr/bin/freecadcmd`). Rendering needs an OpenGL driver (EGL);
everything else is pure Python.

## Changing the design

Edit `macropad_cad/params.py` – every dimension lives there, grouped by component and
by case feature – then run `all`. Values marked `MEASURE` should be checked against
your own modules. Typical edits:

| Want | Change |
|---|---|
| steeper / flatter wedge | `Case.tilt_deg` (front height is set by `Case.front_h`) |
| tighter or looser switch fit | `MXSwitch.hole` |
| modules loose in their pockets | `PrintTol.pocket` |
| different insert size / self-tapping | `Fasteners.insert_d`, `insert_depth` |
| no logo | `Case.logo = ""` |
| move the joystick / displays | `Case.joy_v`, `Case.bar_v`, `Case.center_half` |

Run `check` after every change: it fails (non-zero exit) if any printed part intrudes
into a component, its wiring space, or the joystick sweep, or if parts overlap.

## Package layout

```
params.py      all dimensions (components, fasteners, tolerances, case)
layout.py      derived positions: key grid, module placements, bosses, heights
components.py  reference models of switches, keycaps, Pico 2, displays, KY-023 (+ keep-outs, knob sweep)
parts.py       top_plate, frame, base, bar_spine; print orientation
fit_tests.py   quick coupons for the critical fits
checks.py      interference, clearance and printability checks (manifold3d, trimesh)
export.py      STEP / STL / 3MF / GLB writers, FreeCAD hand-off
freecad_doc.py runs inside FreeCAD to build the .FCStd
render.py      headless OpenGL renderer; renders.py = the standard views
drawings.py    A3 blueprint sheets, section view, 1:1 template
```

## Outputs (`exports/`)

| Folder | Content |
|---|---|
| `stl/`, `3mf/` | print-ready parts in print orientation; `fit-tests/` coupons |
| `step/` | each part in assembled position (editable in any CAD), full colour assembly, reference components |
| `freecad/` | `nyxilab_macropad.FCStd`: printed parts, reference components, a layout spreadsheet |
| `gltf/` | coloured assembly / case for viewers |
| `drawings/` | `nyxilab_macropad_blueprints.pdf` (5 × A3), per-sheet SVG, `template_top_plate_1to1.pdf` |
| `manifest.json` | layout summary, part sizes/volumes, print notes |
