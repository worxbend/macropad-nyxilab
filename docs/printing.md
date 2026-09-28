# Printing

All parts print **without supports** and fit a 180 × 180 mm bed. The files are already
oriented; just drop them on the plate:

- joystick edition: `joystick/cad/exports/3mf/` (and `stl/`)
- rotary edition: `rotary/cad/exports/3mf/` (and `stl/`). The frame and the bar spine
  are the same parts in both editions; the top plate and the base differ, and the
  rotary edition adds the knob.

Or grab the zips from the [downloads page](https://worxbend.github.io/macropad-nyxilab/downloads.html).

| Part | Colour | Bed footprint | Height | Orientation | Settings |
|---|---|---|---|---|---|
| `top_plate` | A (e.g. white) | 159 × 100 mm | 31 mm | top face **down** | 0.2 mm layers, 4 walls, 20 % gyroid, 5 top/bottom layers |
| `frame` | B (accent, e.g. violet) | 159 × 98 mm | 27 mm | flat bottom down | 0.2 mm, 3 walls, 15 % infill |
| `base` | A | 159 × 98 mm | 10 mm | flat bottom down | 0.2 mm, 3 walls, 20 % infill |
| `bar_spine` | any | 69 × 19 mm | 6 mm | flat side down | 0.2 mm, 100 % infill |
| `knob` (rotary) | B, like the frame | Ø30 mm | 19 mm | top face **down** | 0.12–0.16 mm layers, 4 walls, 30 % infill |

Material: PLA or PETG both work. PETG is a little tougher for the self-tapping screws
and the clips of the switches; PLA gives crisper edges. Approximate filament use:
top plate ~60 g, frame ~30 g, base ~50 g, knob ~12 g.

**Make the LEDs visible: print the frame translucent.** The two RGB sticks sit inside
the case, pointing up. In an opaque frame you mostly see them through the gap around the
joystick. Print the **frame** in translucent or "transparent" PETG (clear, frosted, or a
translucent colour) and the whole middle stripe glows in the layer colour. A white PLA
frame also lets some light through. 3 walls and 15 % infill are fine for this; for a more
even glow use 100 % infill, or 0 % infill with 4 walls.

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

**Base tips**
- The four LED-stick posts and the Pico standoffs are small; slow down the outer walls
  a little if they come out stringy.

## The knob (rotary edition)

- Print it **top face down** (as exported). The fluted grip then runs straight up the
  sides and the indicator dimple stays crisp; the top chamfer prints fine at 45°.
- Use fine layers (0.12–0.16 mm) for a smooth grip, 4 walls so the D-bore has solid
  walls, and no brim.
- The bore is a press fit on the 6 mm D-shaft and ends exactly at the shaft tip, so the
  knob stops at the right height (1 mm above the plate). Too tight or too loose? Change
  `Knob.bore_clear` in `rotary/cad/edition.py` (default 0.10 mm; 0.05 is tighter,
  0.20 looser), run `make cad-rotary` and reprint just the knob. It is a 30-minute print.
- Shaft lengths vary between KY-040 batches (15, 20 and 25 mm exist). Measure from the
  top of the encoder body to the shaft tip and set `EncoderKY040.shaft_len`.

## Print the fit tests first

`<edition>/cad/exports/3mf/fit-tests/` (≈1 h in total, a few grams each):

| Coupon | What to check |
|---|---|
| `fit_switch` | three cut-outs 13.9 / 14.0 / 14.1 mm – a switch should click in and not wobble |
| `fit_plate_d19` | 1.9" module drops into its pocket, glass touches the skin, the four M2 holes line up |
| `fit_plate_bar` | 2.25" module fits the pocket, the two M2 holes line up with the hole end |
| `fit_plate_joy` | joystick edition: the knob tilts fully in every direction without touching the bevel |
| `fit_plate_enc` | rotary edition: the KY-040 body drops into the square pocket, the washer and nut clamp it |
| `fit_usb` | Pico 2 on its standoffs, USB-C receptacle sits in the port, a cable plugs in fully |
| `fit_joystick` | joystick edition: the KY-023 holes line up with the four posts |
| `fit_led` | an LED stick lies flat on the two posts and both holes line up |

For the rotary edition, also print the knob early: it is the best test of the shaft fit.

If a coupon is off, measure the part and change the matching value in
`common/cad/macropad_cad/params.py` or the edition's `edition.py` (values marked
`MEASURE` are the likely suspects – see
[design.md](design.md#dimensions-that-came-from-photos-measure-these)), then `make cad`.
Common adjustments:

- switches loose / tight → `MXSwitch.hole` (13.9–14.1)
- modules too tight in their pockets → `PrintTol.pocket` (default 0.25)
- plate lip too tight in the frame → `PrintTol.fit` (default 0.20)
- LED stick holes don't match the posts → `LedStick.hole_pitch` / `hole_y`

## Heat-set inserts

Eight **M3 × 5.7 × 4.6 mm** inserts (Ruthex RX-M3x5.7 / CNC Kitchen standard M3) go into
the lower ends of the top-plate bosses (hole Ø4.0 × 6.2, with a lead-in chamfer).
Lay the plate top-down on the table: the bosses lean 10° (they are vertical in the
assembled case), so tilt the soldering iron to follow the boss axis. Press to flush,
let cool.

No inserts? Change `Fasteners.insert_d` to `2.6` and use M3 self-tapping screws
(e.g. M3 × 8 countersunk plastite) – fine for a few assembly cycles.
