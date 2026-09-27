# Design notes

How the requirements turned into the case, why each part is held the way it is,
and what is verified automatically on every build.

![Section A-A through the joystick, main display, Pico and USB port](images/section_aa.png)

## Requirements (from the brief and sketches)

| Requirement | Where it is solved |
|---|---|
| Wedge case, USB-C at the back, low and centred | 10° slope, port 8.6 mm above the desk, centred on the back wall |
| Top plate / centre plate in another colour / bottom base | three separate prints; the centre frame is the colour band all around |
| 4 keys next to a 76×284 bar display, 8 keys on the right, 1.9" display above a joystick | top layout below, taken 1:1 from the sketch |
| Hand-wired switches and diodes, no custom PCB, no extra USB socket | plate-mount MX cut-outs, Pico 2's own USB-C used through the wall |
| 3D printable, easy to assemble, screws (glue optional) | every part prints without supports; 8 identical case screws; nothing needs glue |

## Top layout

All positions are on the tilted top surface (u → right, v → up the slope), in mm.

| Element | u | v |
|---|---|---|
| 2.25" bar display window centre | 17.04 | 46.60 |
| Key column C0 | 38.53 | rows 19.52 / 38.58 / 57.62 / 76.67 |
| 1.9" display window centre | 80.53 | 67.15 |
| Joystick axis | 80.53 | 27.00 |
| Key columns C1 / C2 | 122.53 / 141.58 | same rows |

Key pitch is 19.05 mm (standard 1u). The centre gap between the key columns is set
by the 1.9" module (62 mm PCB), and the window is kept centred over the joystick even
though the active area is off-centre on that PCB. Plate borders are ~8 mm left/right,
10.5 mm front, 13.8 mm back (the back border carries the engraved logo, set
`logo = ""` in `params.py` to remove it).

## The stack

```
top plate  5.0 mm, tilted 10 deg   colour A   switches, displays, 8 hanging screw bosses
frame      9.9 mm (front) - 27.2 mm (back) walls, 3.0 mm thick   colour B, USB-C port
base       3.0 mm   colour A       Pico cradle, joystick posts, countersinks, feet
```

* The screw bosses belong to the **top plate** and hang straight down (world-vertical)
  inside the frame's corners and edges. M3 heat-set inserts sit at their lower ends,
  0.3 mm above the base. Eight **M3×8 countersunk** screws go up through the base.
  All eight are the same length regardless of the wedge.
* Because the bosses stop 0.3 mm short of the base, the screws pull the base and the
  plate together **through the frame** – the frame is the compression member, so the
  two colour seams close tightly.
* The frame has vertical saddles that wrap the bosses (locating it in X/Y); the plate
  has a 1.6 × 2.5 mm lip that drops inside the frame; the base has lip segments. The
  three parts self-align when stacked. 0.5 mm chamfers at both seams form V-grooves
  that hide small print differences.

## How each component is held

### MX switches (12)
14.0 × 14.0 cut-outs in a 1.5 mm skin (MX spec), with a 15.0 × 15.6 mm relief pocket
below so the clips can spring out under a 5 mm-thick, stiff plate. Switches clip in
from the top; wiring hangs below. Front-row wiring clears the base by 6.5 mm.

### 1.9" display (MSP1901-compatible module, 62 × 29 mm PCB)
Stepped pocket from below: glass (+0.25 mm), then PCB (+0.25 mm), with a 2 mm skin
above the glass and a 1.4 mm × 45° bevelled window around the visible area. A relief
groove clears the header solder joints on the glass side. Four **M2×4** self-tapping
screws through the module's own corner holes into blind pilots (0.7 mm of skin left
above, so nothing shows on top).

### 2.25" bar display (73.15 × 21.08 mm PCB, holes only at one end)
Same pocket scheme. The module has mounting holes at one end only, so the printed
**spine** clamps it: its crossbar is screwed with **2 × M2×8** through the module's two
holes, it arches 3 mm over the FPC / ZIF / parts on the back, and a pad presses the
PCB near the header end (0.2 mm preload). The header points to the front of the case
so the window can stay centred between the two corner bosses.

### KY-023 joystick
The stock knob's dome (Ø26.8) is a sphere around the stick's pivot; at 25° tilt its
rim sweeps down to ~6 mm above the module PCB. Posts hanging from the top plate at the
module's corner holes would sit inside that sweep, so the module stands on **four posts
that grow out of the base**, tilted 10° so the stick is perpendicular to the plate,
with **M3×6** self-tapping screws from above. The plate opening is Ø32 with a
1.5 mm bevel; the knob sweep (+0.4 mm) clears it by 0.7 mm at the underside. A 1.2 mm
relief in the base clears the solder joints under the module. The steel frame of the
stick sits 1.5 mm below the plate surface; the knob stands ~18.6 mm above the plate.

### Pico 2 USB-C clone
The clone has **no mounting holes at the USB end**. It sits on two Ø4.2 standoffs at
the debug-end holes (**M2×5** self-tapping), a centre rail and a forward stop. The USB
end is held by the port itself: the back wall is thinned to 1.2 mm around the
receptacle with a 9.34 × 3.66 mm opening (0.2 mm clearance), and the receptacle face
ends flush with that thin wall. A 14 × 8.5 mm recess outside takes the plug overmold,
so any USB-C cable seats fully. Plugging pushes against the standoffs and the stop;
unplugging pulls against the wall.

## Heights

The front height (18 mm) is set by the joystick: steel frame 1.5 mm below the plate,
12.2 mm mechanism, 1.6 mm PCB and 1.5 mm of solder joints must fit above the base at
the lowest (front) edge of the tilted module. With 10° tilt the back is 35.3 mm. Change
`tilt_deg` / `front_h` in `params.py`; the fit checks will tell you if something no
longer fits (8° gives ~18.3 → 32 mm).

## Automated verification (`make cad-check`)

Every build runs:

* **128 interference pairs** with manifold3d: each printed part against each component
  keep-out (component + solder joints + hand-wiring room), against the joystick knob's
  full sweep (all directions to 25°, plus 1.5 mm press travel), printed parts against
  each other, and components against each other.
* **Clearance report**: joystick joints above the base (1.9 mm), front switch wiring
  above the base (6.5 mm), knob sweep vs opening (0.72 mm), frame wall behind a boss
  (1.95 mm), insert engagement (4.7 mm).
* **Printability report** in print orientation: flags any down-facing surface steeper than
  50° and lists bridges. Current result: only small bridges (logo letters, the USB
  openings ≤ 8 mm, the Ø10.5 feet recesses) and a 1 mm edge chamfer at 50°.

## Dimensions that came from photos (measure these)

The display drawings and the KY-023 measurements are from published data; these values
were estimated from your photos and should be checked with calipers (they are marked
`MEASURE` in `cad/macropad_cad/params.py`):

| Parameter | Default | What to measure |
|---|---|---|
| `Pico2.usb_overhang` | 1.3 | how far the USB-C receptacle sticks out past the PCB edge |
| `Pico2.t` | 1.0 | PCB thickness of your clone |
| `DisplayBar.lcm_x0` | 8.2 | bar module: hole-end PCB edge to the start of the glass |
| `DisplayBar.hole_x`, `hole_y` | 2.5, 8.15 | bar module hole position from the end / centre line |
| `DisplayBar.pcb_t` | 1.2 | bar module PCB thickness |
| `Display19.hole_d` | 2.2 | 1.9" module hole diameter (M2 must pass) |
| `JoystickKY023.hole_dx`, `hole_dy` | 26.7, 20.3 | joystick hole spacing (sources say 26.5–26.7 × 19.0–20.3) |

The fit-test coupons check exactly these.
