"""Simplified models of the bought parts, used for renders and fit checks.

Each ``*_model`` returns named solids in the component's own frame (see the
docstrings in ``params``).  ``*_keepout`` returns the volume that the case must
leave free: the part itself plus room for solder joints and hand wiring.
Layout places them in the world with the ``Layout.loc_*`` locations; the centre
control (joystick or encoder) comes from the edition (``L.ed``).
"""

from __future__ import annotations

import math

from build123d import (
    Align,
    Axis,
    Box,
    Circle,
    Cylinder,
    Face,
    Location,
    Part,
    Plane,
    Polyline,
    Pos,
    Rectangle,
    RectangleRounded,
    RegularPolygon,
    Rot,
    Solid,
    Sphere,
    Wire,
    extrude,
    make_face,
    revolve,
)

from .layout import Layout
from .params import P

MIN = (Align.MIN, Align.MIN, Align.MIN)
CXY = (Align.CENTER, Align.CENTER, Align.MIN)


def box(x0, x1, y0, y1, z0, z1) -> Part:
    return Pos(x0, y0, z0) * Box(x1 - x0, y1 - y0, z1 - z0, align=MIN)


def cyl(x, y, z0, z1, d) -> Part:
    return Pos(x, y, z0) * Cylinder(d / 2, z1 - z0, align=CXY)


# ---------------------------------------------------------------- switches
def switch_model(p=P) -> dict[str, Part]:
    s = p.sw
    below = box(-s.body / 2, s.body / 2, -s.body / 2, s.body / 2, -s.body_below, 0)
    top = extrude(Rectangle(s.top_base, s.top_base), amount=s.top_h, taper=17)
    stem = Pos(0, 0, s.top_h) * (Box(4.1, 1.2, 3.6, align=CXY) + Box(1.2, 4.1, 3.6, align=CXY))
    post = cyl(0, 0, -s.body_below - 3.0, -s.body_below, 4.0)
    pins = cyl(-3.81, 2.54, -s.body_below - s.pins_below, -s.body_below, 1.0) + cyl(
        2.54, 5.08, -s.body_below - s.pins_below, -s.body_below, 1.0
    )
    return {"housing": below + top + post, "stem": stem, "pins": pins}


def keycap_model(p=P) -> Part:
    s = p.sw
    taper = math.degrees(math.atan((s.cap - s.cap_top) / 2 / s.cap_h))
    cap = extrude(RectangleRounded(s.cap, s.cap, 1.6), amount=s.cap_h, taper=taper)
    return Pos(0, 0, s.cap_lift) * cap


def switch_keepout(p=P) -> Part:
    s = p.sw
    # 0.05 mm shrink where the body passes through the 14.0 hole (touching by design)
    through = box(-s.body / 2 + 0.05, s.body / 2 - 0.05, -s.body / 2 + 0.05, s.body / 2 - 0.05, -s.body_below, 0.5)
    wiring = box(-s.body / 2, s.body / 2, -s.body / 2, s.body / 2,
                 -(s.body_below + s.pins_below + s.wiring_below), -s.body_below)
    return through + wiring


# -------------------------------------------------------------------- Pico
def pico_model(p=P) -> dict[str, Part]:
    c = p.pico
    pcb = box(0, c.length, -c.width / 2, c.width / 2, 0, c.t)
    for sy in (-1, 1):
        pcb -= cyl(c.length - c.hole_from_end, sy * c.hole_spacing / 2, -1, 2, c.hole_d)
        for i in range(20):
            x = c.first_pin + i * c.pin_pitch
            pcb -= cyl(x, sy * c.row_spacing / 2, -1, 2, 1.0)
            pcb -= cyl(x, sy * c.width / 2, -1, 2, 1.1)  # castellations
    usb_profile = RectangleRounded(c.usb_h, c.usb_w, 1.2)
    usb = Pos(-c.usb_overhang, 0, c.t + c.usb_h / 2) * Rot(0, 90, 0) * extrude(usb_profile, amount=c.usb_l)
    chip = box(29.5, 36.5, -3.5, 3.5, c.t, c.t + 0.9)
    flash = box(15.5, 19.5, -7.5, -3.5, c.t, c.t + 0.8)
    button = box(10.0, 13.5, -7.2, -3.2, c.t, c.t + 1.5)
    return {"pcb": pcb, "usb": usb, "chips": chip + flash + button}


def pico_keepout(p=P) -> Part:
    c = p.pico
    body = box(0, c.length, -c.width / 2 - 1.0, c.width / 2 + 1.0, 0, c.t + c.parts_h + c.wiring_top)
    # solder joints under the two pin rows and the USB-C shell tabs (the centre is flat)
    for sy in (-1, 1):
        for i in range(20):
            body += cyl(c.first_pin + i * c.pin_pitch, sy * c.row_spacing / 2, -c.joints_below, 0, 2.4)
    body += box(0, 8.0, -5.5, 5.5, -c.joints_below, 0)
    usb_profile = RectangleRounded(c.usb_h, c.usb_w, 1.2)
    usb = Pos(-c.usb_overhang, 0, c.t + c.usb_h / 2) * Rot(0, 90, 0) * extrude(usb_profile, amount=c.usb_l)
    return body + usb


# ---------------------------------------------------------- 1.9" display
def d19_model(p=P) -> dict[str, Part]:
    d = p.d19
    z_pcb_top = -(d.lcm_t + d.tape_t)
    z_pcb_bot = z_pcb_top - d.pcb_t
    lcm = box(d.lcm_x0, d.lcm_x0 + d.lcm_l, -d.lcm_w / 2, d.lcm_w / 2, z_pcb_top, 0)
    aa = box(d.aa_x0, d.aa_x0 + d.aa_l, -d.aa_w / 2, d.aa_w / 2, -0.02, 0.01)
    pcb = box(0, d.pcb_l, -d.pcb_w / 2, d.pcb_w / 2, z_pcb_bot, z_pcb_top)
    for x in (d.hole_inset, d.pcb_l - d.hole_inset):
        for y in (-1, 1):
            pcb -= cyl(x, y * (d.pcb_w / 2 - d.hole_inset), z_pcb_bot - 1, 1, d.hole_d)
    for i in range(8):
        pcb -= cyl(d.header_x, -8.89 + i * 2.54, z_pcb_bot - 1, 1, 1.0)
    back = box(9.0, 50.0, -9.0, 9.0, z_pcb_bot - d.back_h, z_pcb_bot)
    return {"lcm": lcm - aa, "screen": aa, "pcb": pcb, "back": back}


def d19_keepout(p=P) -> Part:
    d = p.d19
    z_pcb_top = -(d.lcm_t + d.tape_t)
    z_pcb_bot = z_pcb_top - d.pcb_t
    ko = box(d.lcm_x0, d.lcm_x0 + d.lcm_l + d.fpc_allow, -d.lcm_w / 2, d.lcm_w / 2, z_pcb_top, -0.02)
    ko += box(0.02, d.pcb_l - 0.02, -d.pcb_w / 2 + 0.02, d.pcb_w / 2 - 0.02, z_pcb_bot, z_pcb_top)
    ko += box(9.0, 50.0, -9.0, 9.0, z_pcb_bot - d.back_h, z_pcb_bot)
    # header: joints on the front face, wires leaving the back
    ko += box(0.6, 2.4, -9.8, 9.8, z_pcb_top, z_pcb_top + 0.9)
    ko += box(0.0, 3.5, -10.5, 10.5, z_pcb_bot - 6.0, z_pcb_bot)
    return ko


# ------------------------------------------------------- 2.25" bar display
def bar_model(p=P) -> dict[str, Part]:
    b = p.bar
    z_pcb_top = -(b.lcm_t + b.tape_t)
    z_pcb_bot = z_pcb_top - b.pcb_t
    lcm = box(b.lcm_x0, b.lcm_x0 + b.lcm_l, -b.lcm_w / 2, b.lcm_w / 2, z_pcb_top, 0)
    aa = box(b.aa_x0, b.aa_x0 + b.aa_l, -b.aa_w / 2, b.aa_w / 2, -0.02, 0.01)
    pcb = box(0, b.pcb_l, -b.pcb_w / 2, b.pcb_w / 2, z_pcb_bot, z_pcb_top)
    for y in (-1, 1):
        pcb -= cyl(b.hole_x, y * b.hole_y, z_pcb_bot - 1, 1, b.hole_d)
    for i in range(8):
        pcb -= cyl(b.pcb_l - b.header_x_from_end, -8.89 + i * 2.54, z_pcb_bot - 1, 1, 1.0)
    back = box(5.9, 52.8, -8.0, 8.0, z_pcb_bot - b.back_h, z_pcb_bot)
    return {"lcm": lcm - aa, "screen": aa, "pcb": pcb, "back": back}


def bar_keepout(p=P) -> Part:
    b = p.bar
    z_pcb_top = -(b.lcm_t + b.tape_t)
    z_pcb_bot = z_pcb_top - b.pcb_t
    ko = box(b.lcm_x0 - b.fpc_allow, b.lcm_x0 + b.lcm_l, -b.lcm_w / 2, b.lcm_w / 2, z_pcb_top, -0.02)
    ko += box(0.02, b.pcb_l - 0.02, -b.pcb_w / 2 + 0.02, b.pcb_w / 2 - 0.02, z_pcb_bot, z_pcb_top)
    ko += box(5.9, 52.8, -8.0, 8.0, z_pcb_bot - b.back_h, z_pcb_bot)
    hx = b.pcb_l - b.header_x_from_end
    ko += box(hx - 0.9, hx + 0.9, -9.8, 9.8, z_pcb_top, z_pcb_top + 0.9)
    ko += box(b.pcb_l - 3.0, b.pcb_l, -10.5, 10.5, z_pcb_bot - 6.0, z_pcb_bot)
    return ko


# --------------------------------------------------------------- LED stick
def led_stick_model(p=P) -> dict[str, Part]:
    """8 x WS2812B stick in its frame (z = 0 on the PCB underside, LEDs up)."""
    s = p.led
    pcb = box(-s.pcb_l / 2, s.pcb_l / 2, -s.pcb_w / 2, s.pcb_w / 2, 0, s.pcb_t)
    for sx in (-1, 1):
        pcb -= cyl(sx * s.hole_pitch / 2, s.hole_y, -1, s.pcb_t + 1, s.hole_d)
    xs = [(i - (s.led_n - 1) / 2) * s.led_pitch for i in range(s.led_n)]
    q = s.led_size / 2
    leds = [box(x - q, x + q, s.led_y - q, s.led_y + q, s.pcb_t, s.pcb_t + s.led_h) for x in xs]
    lens = [cyl(x, s.led_y, s.pcb_t + s.led_h - 0.2, s.pcb_t + s.led_h + 0.01, 3.6) for x in xs]
    caps = [box(x - 1.0, x + 1.0, s.cap_y - 0.62, s.cap_y + 0.62, s.pcb_t, s.pcb_t + 0.6) for x in xs]
    fuse = lambda solids: solids[0].fuse(*solids[1:])  # noqa: E731
    return {"pcb": pcb, "led": fuse(leds), "lens": fuse(lens), "caps": fuse(caps)}


def led_stick_keepout(p=P) -> Part:
    """Stick + end-pad joints + the wires leaving both ends + a little light clearance."""
    s = p.led
    x0, x1, y0, y1 = -s.pcb_l / 2, s.pcb_l / 2, -s.pcb_w / 2, s.pcb_w / 2
    ko = box(x0 - 0.3, x1 + 0.3, y0 - 0.3, y1 + 0.3, 0, s.pcb_t + s.led_h + s.light_clear)
    for sx in (-1, 1):
        end = sx * s.pcb_l / 2
        lo, hi = sorted((end - sx * s.pad_zone, end + sx * s.wire_out))
        ko += box(lo, hi, y0 + 0.5, y1 - 0.5, -s.joints_below, s.pcb_t + 0.6)
    return ko


# ------------------------------------------------------------------ placed
def placed_components(L: Layout) -> dict[str, dict[str, Part]]:
    """World-space component models grouped by component, for renders."""
    p = L.p
    out: dict[str, dict[str, Part]] = {}
    sw = switch_model(p)
    cap = keycap_model(p)
    for k in L.keys:
        loc = L.ploc * Pos(k.u, k.v, 0)
        out[f"switch {k.name}"] = {n: loc * s for n, s in sw.items()}
        out[f"keycap {k.name}"] = {"cap": loc * cap}
    out["pico"] = {n: L.loc_pico * s for n, s in pico_model(p).items()}
    out["display 1.9"] = {n: L.loc_d19 * s for n, s in d19_model(p).items()}
    out["display bar"] = {n: L.loc_bar * s for n, s in bar_model(p).items()}
    out.update(L.ed.components(L))
    stick = led_stick_model(p)
    for name, loc in L.led_locs.items():
        out[name] = {n: loc * s for n, s in stick.items()}
    return out


def placed_keepouts(L: Layout) -> dict[str, Part]:
    p = L.p
    ko: dict[str, Part] = {}
    s = switch_keepout(p)
    for k in L.keys:
        ko[f"switch {k.name}"] = L.ploc * Pos(k.u, k.v, 0) * s
    ko["pico"] = L.loc_pico * pico_keepout(p)
    ko["display 1.9"] = L.loc_d19 * d19_keepout(p)
    ko["display bar"] = L.loc_bar * bar_keepout(p)
    ko.update(L.ed.keepouts(L))
    stick = led_stick_keepout(p)
    for name, loc in L.led_locs.items():
        ko[name] = loc * stick
    return ko
