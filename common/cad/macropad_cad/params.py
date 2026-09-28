"""All dimensions of the macropad, in millimetres.

Everything that ends up in a printed part is derived from the values in this
file.  Values marked ``MEASURE`` come from datasheets or photos of the actual
modules and should be checked with calipers before printing the full case
(print the fit-test coupons in ``exports/stl/fit-tests`` first).

The centre control (KY-023 joystick or KY-040 encoder + knob) is not here: each
edition keeps its own parameters next to its geometry (``<edition>/cad/edition.py``).

Coordinate conventions (see docs/design.md):

* World: X = left -> right, Y = front (user) -> back (USB), Z = up, desk at z=0.
* Plate frame: the top surface of the tilted top plate.  ``u`` runs along X,
  ``v`` runs up the slope towards the back, ``w`` is the outward normal
  (w = 0 on the top surface, w = -T on the underside).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field


# --------------------------------------------------------------------------
# Printing / fasteners
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class PrintTol:
    fit: float = 0.20  # clearance per side for parts that slide together
    pocket: float = 0.25  # clearance per side for modules sitting in pockets
    boss_gap: float = 0.30  # gap between boss ends and the base (frame takes the clamp load)


@dataclass(frozen=True)
class Fasteners:
    # Case: M3 countersunk (ISO 10642) from below into heat-set inserts in plate bosses
    m3_clear_d: float = 3.4
    m3_csk_d: float = 6.8  # countersink diameter at the bottom surface (90 deg)
    insert_d: float = 4.0  # hole for M3 x 5.7 x 4.6 heat-set insert (Ruthex/CNC Kitchen style)
    insert_depth: float = 6.2
    case_screw_len: float = 8.0
    boss_d: float = 8.0
    # Self-tapping holes for screws driven straight into plastic
    m2_pilot_d: float = 1.7
    m2_clear_d: float = 2.4
    m3_pilot_d: float = 2.5


# --------------------------------------------------------------------------
# Components
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class MXSwitch:
    pitch: float = 19.05
    hole: float = 14.0  # plate cut-out (MX spec 14.0, tune for your printer)
    skin: float = 1.5  # plate thickness where the clips engage (MX spec 1.5)
    pocket_u: float = 15.0  # relief under the skin, east/west
    pocket_v: float = 15.6  # relief under the skin, north/south (clip side)
    body: float = 14.0
    body_below: float = 5.0  # housing below the plate top surface
    pins_below: float = 3.3  # pins below the housing
    wiring_below: float = 2.5  # diode legs + wire below the pin tips (keep-out)
    top_base: float = 15.6
    top_h: float = 6.6
    cap: float = 18.0
    cap_top: float = 12.7
    cap_h: float = 8.5
    cap_lift: float = 5.2  # keycap skirt height above the plate at rest


@dataclass(frozen=True)
class Pico2:
    """Raspberry Pi Pico 2 compatible board with USB-C (red "RP2350 Pico2" clone).

    The clone in the photos has NO mounting holes at the USB end, only the two
    at the debug end.  The USB end is captured by the case port instead.
    """

    length: float = 51.0
    width: float = 21.0
    t: float = 1.0  # MEASURE (official Pico: 1.0)
    hole_d: float = 2.1
    hole_from_end: float = 2.0  # debug-end holes, from the far edge
    hole_spacing: float = 11.4
    usb_w: float = 8.94
    usb_h: float = 3.26
    usb_l: float = 7.35
    usb_overhang: float = 1.3  # MEASURE: receptacle face beyond the PCB edge
    pin_pitch: float = 2.54
    row_spacing: float = 17.78
    first_pin: float = 1.61  # pin 1 centre from the USB edge
    parts_h: float = 1.6  # tallest part on top besides USB-C (BOOTSEL, inductor)
    wiring_top: float = 3.0  # wires soldered on top (keep-out)
    joints_below: float = 1.0  # solder joints below the PCB
    standoff_h: float = 3.0


@dataclass(frozen=True)
class Display19:
    """1.9" 170x320 ST7789 IPS module, 8-pin (MSP1901-compatible, FPC T190X7-C30).

    Frame: x along the PCB length from the HEADER end, y across (centred).
    """

    pcb_l: float = 62.0
    pcb_w: float = 29.0
    pcb_t: float = 1.6
    hole_d: float = 2.2  # MEASURE
    hole_inset: float = 2.0  # hole centres from both edges (57.99 x 25.0 grid)
    lcm_l: float = 49.72
    lcm_w: float = 25.8
    lcm_t: float = 1.43
    tape_t: float = 0.30
    lcm_x0: float = 6.14  # LCM starts 6.14 from the header end
    aa_l: float = 42.72
    aa_w: float = 22.695
    aa_x0: float = 7.64  # active area starts 7.64 from the header end
    va_margin: float = 0.5  # visible area = AA + 0.5 per side
    back_h: float = 2.1  # SMD parts + ZIF on the back
    header_x: float = 1.5  # pin centres from the header end
    fpc_allow: float = 1.6  # FPC bend beyond the LCM at the far end
    res: tuple = (170, 320)


@dataclass(frozen=True)
class DisplayBar:
    """2.25" 76x284 ST7789P3 bar module, 8-pin ("VERtft 2.25 2.0").

    Frame: x along the PCB length from the HOLE end (two holes only there),
    y across (centred).  The 8-pin header is at the far end (x = pcb_l).
    """

    pcb_l: float = 73.15
    pcb_w: float = 21.08
    pcb_t: float = 1.2  # MEASURE (module is 3.56 thick in total)
    hole_d: float = 2.2
    hole_x: float = 2.5  # MEASURE (photo)
    hole_y: float = 8.15  # MEASURE (photo) +/- from the centre line
    lcm_l: float = 62.5
    lcm_w: float = 17.9
    lcm_t: float = 2.36
    tape_t: float = 0.10
    lcm_x0: float = 8.2  # MEASURE (photo): LCM starts 8.2 from the hole end
    aa_l: float = 55.29
    aa_w: float = 14.80
    aa_from_lcm_end: float = 1.61  # AA ends 1.61 before the LCM's header-side end
    va_margin: float = 0.2
    back_h: float = 2.0
    header_x_from_end: float = 1.3
    fpc_allow: float = 1.6
    free_back_zone: tuple = (3.0, 16.0)  # PCB back is empty this far from the header end
    res: tuple = (76, 284)

    @property
    def aa_x0(self) -> float:
        return self.lcm_x0 + self.lcm_l - self.aa_from_lcm_end - self.aa_l

    @property
    def aa_cx(self) -> float:
        return self.aa_x0 + self.aa_l / 2


@dataclass(frozen=True)
class LedStick:
    """RGB LED stick: 8 x WS2812B (5050) on a 53 x 10 mm PCB, CJMCU-2812-8 style.

    Frame: origin at the PCB centre on its UNDERSIDE, x along the length from the
    DIN end (-x) to the DOUT end (+x), y across (+y = mounting-hole / capacitor
    side), z up (LEDs on top).  Solder pads are on the back at both ends.
    Photo-measured (MEASURE); two of these are chained on the base.
    """

    pcb_l: float = 53.0  # MEASURE
    pcb_w: float = 10.0  # MEASURE
    pcb_t: float = 1.6
    led_n: int = 8
    led_pitch: float = 6.6  # MEASURE (53 / 8)
    led_size: float = 5.0
    led_h: float = 1.6
    led_y: float = -1.25  # MEASURE: LED row off the centre line, away from the holes
    cap_y: float = 3.1  # 0805 decoupling caps between the LEDs and the long edge
    hole_d: float = 2.5  # MEASURE
    hole_pitch: float = 26.4  # MEASURE: holes sit between LEDs 2-3 and 6-7
    hole_y: float = 2.9  # MEASURE
    pad_zone: float = 4.0  # back-side pads (DIN/DOUT, 5V, GND) this far from each end
    joints_below: float = 1.5
    wire_out: float = 4.0  # wires leave the end pads within this distance
    light_clear: float = 1.0  # kept free above the LEDs


# --------------------------------------------------------------------------
# Case
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Case:
    tilt_deg: float = 10.0
    depth: float = 98.0  # D, world Y
    front_h: float = 18.0  # top surface height at the front edge
    base_t: float = 3.0
    plate_t: float = 5.0
    wall: float = 3.0
    corner_r: float = 7.0
    top_chamfer: float = 1.0
    base_chamfer: float = 0.8
    plate_lip_t: float = 1.6
    plate_lip_h: float = 2.5
    base_lip_t: float = 1.6
    base_lip_h: float = 2.0
    boss_inset: float = 6.2  # boss centres from the outer faces
    # layout (plate frame)
    bar_pcb_u0: float = 6.5
    key_gap_after_bar: float = 2.0
    center_half: float = 42.0  # left key column -> centre axis -> first right column
    right_margin: float = 8.37
    rows_v0: float = 10.0
    centre_v: float = 27.0  # axis of the centre control (joystick or knob), under the 1.9" display
    # RGB LED sticks on the base (both editions), one on each side of the Pico, LEDs up
    led_dx: float = 32.0  # stick centre lines from the Pico axis (world x)
    led_y: float = 61.0  # stick centres (world y): clear of the back bosses and the lip
    led_post_d: float = 5.0
    led_post_h: float = 3.0  # PCB underside above the floor: room for the end-pad joints and wires
    disp_skin: float = 2.0  # plate material above the display glass
    window_chamfer: float = 1.4
    window_margin: float = 0.15
    bar_v: float = 46.6  # bar display active-area centre (header towards the front)
    usb_thin_wall: float = 1.2
    usb_recess: tuple = (14.0, 8.5, 3.0)  # w, h, r of the outer recess (cable overmold)
    usb_open_clear: float = 0.20
    feet_d: float = 10.5
    feet_depth: float = 1.0
    feet_inset: float = 16.0
    logo: str = "NYXILAB"
    logo_h: float = 3.6
    logo_depth: float = 0.6


@dataclass(frozen=True)
class Params:
    tol: PrintTol = field(default_factory=PrintTol)
    fas: Fasteners = field(default_factory=Fasteners)
    sw: MXSwitch = field(default_factory=MXSwitch)
    pico: Pico2 = field(default_factory=Pico2)
    d19: Display19 = field(default_factory=Display19)
    bar: DisplayBar = field(default_factory=DisplayBar)
    led: LedStick = field(default_factory=LedStick)
    case: Case = field(default_factory=Case)


P = Params()
