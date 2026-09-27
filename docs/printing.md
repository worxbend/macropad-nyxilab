# Printing

All parts print **without supports** and fit a 180 × 180 mm bed. The files in
`cad/exports/stl` and `cad/exports/3mf` are already oriented; just drop them on the plate.

| Part | Colour | Bed footprint | Height | Orientation | Settings |
|---|---|---|---|---|---|
| `top_plate` | A (e.g. white) | 159 × 100 mm | 31 mm | top face **down** | 0.2 mm layers, 4 walls, 20 % gyroid, 5 top/bottom layers |
| `frame` | B (accent, e.g. violet) | 159 × 98 mm | 27 mm | flat bottom down | 0.2 mm, 3 walls, 15 % infill |
| `base` | A | 159 × 98 mm | 10 mm | flat bottom down | 0.2 mm, 3 walls, 20 % infill |
| `bar_spine` | any | 69 × 19 mm | 6 mm | flat side down | 0.2 mm, 100 % infill |

Material: PLA or PETG both work. PETG is a little tougher for the self-tapping screws
and the clips of the switches; PLA gives crisper edges. Approximate filament use:
top plate ~60 g, frame ~30 g, base ~50 g.

**Top plate tips**
- Printing the top face down gives a flat, bed-textured top surface. A textured PEI
  sheet makes a nice matte finish; a smooth sheet gives gloss.
- The 1.5 mm switch skin and the 2 mm display skin are the first layers – level the bed
  well and use a normal (not squished) first layer so the 14.0 mm holes stay accurate.
- The screw bosses stand up to 26 mm tall; print them at normal speed with the part fan on.

**Frame tips**
- Use the brim only if your printer lifts corners; the 0.5 mm bottom chamfer already
  compensates elephant's foot.
- The USB-C opening has two short bridges (≤ 8 mm) – default bridge settings are fine.

## Print the fit tests first

`cad/exports/3mf/fit-tests/` (≈1 h in total, a few grams each):

| Coupon | What to check |
|---|---|
| `fit_switch` | three cut-outs 13.9 / 14.0 / 14.1 mm – a switch should click in and not wobble |
| `fit_plate_d19` | 1.9" module drops into its pocket, glass touches the skin, the four M2 holes line up |
| `fit_plate_bar` | 2.25" module fits the pocket, the two M2 holes line up with the hole end |
| `fit_plate_joy` | the knob tilts fully in every direction without touching the bevel |
| `fit_usb` | Pico 2 on its standoffs, USB-C receptacle sits in the port, a cable plugs in fully |
| `fit_joystick` | the KY-023 holes line up with the four posts |

If a coupon is off, measure the part and change the matching value in
`cad/macropad_cad/params.py` (values marked `MEASURE` are the likely suspects – see
[design.md](design.md#dimensions-that-came-from-photos-measure-these)), then `make cad`.
Common adjustments:

- switches loose / tight → `MXSwitch.hole` (13.9–14.1)
- modules too tight in their pockets → `PrintTol.pocket` (default 0.25)
- plate lip too tight in the frame → `PrintTol.fit` (default 0.20)

## Heat-set inserts

Eight **M3 × 5.7 × 4.6 mm** inserts (Ruthex RX-M3x5.7 / CNC Kitchen standard M3) go into
the lower ends of the top-plate bosses (hole Ø4.0 × 6.2, with a lead-in chamfer).
Lay the plate top-down on the table: the bosses lean 10° (they are vertical in the
assembled case), so tilt the soldering iron to follow the boss axis. Press to flush,
let cool.

No inserts? Change `Fasteners.insert_d` to `2.6` and use M3 self-tapping screws
(e.g. M3 × 8 countersunk plastite) – fine for a few assembly cycles.
