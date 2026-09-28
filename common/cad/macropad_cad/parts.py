"""The printed parts, built in world (assembled) coordinates.

top_plate  - tilted switch/display plate with hanging screw bosses (colour A)
frame      - wedge-shaped wall ring, carries the USB-C port (colour B)
base       - flat bottom with Pico cradle, LED-stick posts, feet (colour A/C)
bar_spine  - small clamp that holds the 2.25" bar display in its pocket

The edition adds the centre-control mount (``L.ed.plate_cuts`` / ``base_features``)
and its own parts (``L.ed.parts``, e.g. the rotary knob).
``print_pose`` returns each part rotated/translated onto the print bed.
"""

from __future__ import annotations

import math
from functools import lru_cache

from build123d import (
    Align,
    Axis,
    Box,
    Cone,
    Cylinder,
    Face,
    FontStyle,
    Location,
    Part,
    Pos,
    RectangleRounded,
    Rot,
    Text,
    Vector,
    chamfer,
    extrude,
    offset,
)

from .components import CXY, MIN, box, cyl
from .layout import Layout

BIG = 400.0


# ----------------------------------------------------------------- helpers
def outline(L: Layout, inset: float = 0.0) -> Face:
    c = L.p.case
    return Pos(L.W / 2, L.D / 2) * RectangleRounded(L.W - 2 * inset, L.D - 2 * inset, max(c.corner_r - inset, 0.5))


def prism(face: Face, z0: float, z1: float) -> Part:
    return Pos(0, 0, z0) * extrude(face, amount=z1 - z0)


def plate_band(L: Layout, w0: float, w1: float) -> Part:
    """Slab between two planes parallel to the top surface (plate frame w)."""
    return L.ploc * box(-BIG / 2, BIG, -BIG / 2, BIG, w0, w1)


def rrect_prism(x: float, y: float, sx: float, sy: float, r: float, z0: float, z1: float) -> Part:
    r = min(r, sx / 2 - 0.01, sy / 2 - 0.01)
    return Pos(x, y, z0) * extrude(RectangleRounded(sx, sy, r), amount=z1 - z0)


def rrect_chamfer_cut(x: float, y: float, sx: float, sy: float, r: float, z_top: float, ch: float) -> Part:
    """45-degree bevel into a surface at z_top (local frame), widening upwards."""
    extra = 0.3
    prof = RectangleRounded(sx + 2 * (ch + extra), sy + 2 * (ch + extra), r + ch + extra)
    return Pos(x, y, z_top + extra) * extrude(prof, amount=ch + extra, dir=(0, 0, -1), taper=45)


def prism_y(cx: float, cz: float, w: float, h: float, r: float, y0: float, y1: float) -> Part:
    """Rounded-rectangle prism running along +Y (for openings in the back wall)."""
    r = min(r, w / 2 - 0.01, h / 2 - 0.01)
    solid = extrude(RectangleRounded(w, h, r), amount=y1 - y0)
    return Pos(cx, y0, cz) * Rot(-90, 0, 0) * solid


def outer_edges_of_face(part: Part, normal: Vector, tol: float = 0.999):
    faces = [f for f in part.faces() if f.normal_at().dot(normal) > tol]
    faces.sort(key=lambda f: -f.area)
    return faces[0].outer_wire().edges()


# --------------------------------------------------------------- top plate
@lru_cache(maxsize=None)
def top_plate(L: Layout) -> Part:
    p = L.p
    c, sw, tol, fas = p.case, p.sw, p.tol, p.fas
    T = c.plate_t

    body = prism(outline(L), -1, L.H_b + 5) & plate_band(L, -T, 0)
    body = chamfer(outer_edges_of_face(body, L.plane.z_dir), c.top_chamfer)
    body = chamfer(outer_edges_of_face(body, -L.plane.z_dir), 0.5)

    # locating lip that drops inside the frame
    ring = outline(L, c.wall + tol.fit) - outline(L, c.wall + tol.fit + c.plate_lip_t)
    body += prism(ring, 0, L.H_b + 5) & plate_band(L, -T - c.plate_lip_h, -T + 0.5)

    # screw bosses (world-vertical) with heat-set inserts at their lower ends
    z_boss = c.base_t + tol.boss_gap
    for _, x, y in L.bosses:
        body += (Pos(x, y, z_boss) * Cylinder(fas.boss_d / 2, BIG, align=CXY)) & plate_band(L, -BIG, -1.5)
    for _, x, y in L.bosses:
        body -= Pos(x, y, z_boss - 1) * Cylinder(fas.insert_d / 2, fas.insert_depth + 1, align=CXY)
        body -= Pos(x, y, z_boss - 0.01) * Cone(fas.insert_d / 2 + 0.5, fas.insert_d / 2, 0.5, align=CXY)

    # MX switch cut-outs with clip relief under a 1.5 mm skin
    for k in L.keys:
        body -= L.ploc * box(k.u - sw.hole / 2, k.u + sw.hole / 2, k.v - sw.hole / 2, k.v + sw.hole / 2, -T - 5, 1)
        body -= L.ploc * box(k.u - sw.pocket_u / 2, k.u + sw.pocket_u / 2, k.v - sw.pocket_v / 2,
                             k.v + sw.pocket_v / 2, -T - 5, -sw.skin)

    body -= L.loc_d19 * d19_cuts(L)
    body -= L.loc_bar * bar_cuts(L)
    body -= L.ed.plate_cuts(L)  # joystick opening or encoder mount

    if c.logo:
        txt = Text(c.logo, font_size=c.logo_h, font_style=FontStyle.BOLD)
        v_logo = (L.rows[3] + p.sw.cap / 2 + L.V) / 2
        body -= L.ploc * Pos(L.u_c, v_logo, -c.logo_depth) * extrude(txt, amount=c.logo_depth + 1)
    return body


def d19_cuts(L: Layout) -> Part:
    """Pockets for the 1.9" module, in the module frame (z=0 = glass top)."""
    p = L.p
    d, c, pk = p.d19, p.case, p.tol.pocket
    s = c.disp_skin
    z_under = -(c.plate_t - s) - 3
    z_pcb_top = -(d.lcm_t + d.tape_t)
    va = d.va_margin + c.window_margin
    wx0, wx1 = d.aa_x0 - va, d.aa_x0 + d.aa_l + va
    wy = d.aa_w / 2 + va
    cuts = rrect_prism((wx0 + wx1) / 2, 0, wx1 - wx0, 2 * wy, 0.8, -0.5, s + 1)
    cuts += rrect_chamfer_cut((wx0 + wx1) / 2, 0, wx1 - wx0, 2 * wy, 0.8, s, c.window_chamfer)
    cuts += box(d.lcm_x0 - pk, d.lcm_x0 + d.lcm_l + d.fpc_allow + pk, -d.lcm_w / 2 - pk, d.lcm_w / 2 + pk, z_under, 0)
    cuts += box(-pk, d.pcb_l + pk, -d.pcb_w / 2 - pk, d.pcb_w / 2 + pk, z_under, z_pcb_top)
    cuts += box(-pk, 3.2, -10.6, 10.6, z_under, z_pcb_top + 1.2)  # header solder joints
    for x in (d.hole_inset, d.pcb_l - d.hole_inset):
        for sy in (-1, 1):
            cuts += cyl(x, sy * (d.pcb_w / 2 - d.hole_inset), z_under, z_pcb_top + 3.0, p.fas.m2_pilot_d)
    return cuts


def bar_cuts(L: Layout) -> Part:
    """Pockets for the 2.25" bar module, in the module frame (z=0 = glass top)."""
    p = L.p
    b, c, pk = p.bar, p.case, p.tol.pocket
    s = c.disp_skin
    z_under = -(c.plate_t - s) - 3
    z_pcb_top = -(b.lcm_t + b.tape_t)
    va = b.va_margin + c.window_margin
    wx0, wx1 = b.aa_x0 - va, b.aa_x0 + b.aa_l + va
    wy = b.aa_w / 2 + va
    cuts = rrect_prism((wx0 + wx1) / 2, 0, wx1 - wx0, 2 * wy, 0.8, -0.5, s + 1)
    cuts += rrect_chamfer_cut((wx0 + wx1) / 2, 0, wx1 - wx0, 2 * wy, 0.8, s, c.window_chamfer)
    cuts += box(b.lcm_x0 - b.fpc_allow - pk, b.lcm_x0 + b.lcm_l + pk, -b.lcm_w / 2 - pk, b.lcm_w / 2 + pk, z_under, 0)
    cuts += box(-pk, b.pcb_l + pk, -b.pcb_w / 2 - pk, b.pcb_w / 2 + pk, z_under, z_pcb_top)
    cuts += box(b.pcb_l - 3.2, b.pcb_l + pk, -10.3, 10.3, z_under, z_pcb_top + 1.2)
    for sy in (-1, 1):
        cuts += cyl(b.hole_x, sy * b.hole_y, z_under, z_pcb_top + 3.66, p.fas.m2_pilot_d)
    return cuts


# ------------------------------------------------------------------- frame
@lru_cache(maxsize=None)
def frame(L: Layout) -> Part:
    p = L.p
    c, tol, fas, pc = p.case, p.tol, p.fas, p.pico
    ring = outline(L) - outline(L, c.wall)
    body = prism(ring, c.base_t, L.H_b + 5) & plate_band(L, -BIG, -c.plate_t)
    # V-grooves at the colour seams
    body = chamfer(outer_edges_of_face(body, L.plane.z_dir), 0.5)
    body = chamfer(outer_edges_of_face(body, Vector(0, 0, -1)), 0.5)
    for _, x, y in L.bosses:
        body -= Pos(x, y, 0) * Cylinder(fas.boss_d / 2 + tol.fit + 0.05, BIG, align=CXY)

    # USB-C: thin wall that captures the receptacle + outer recess for the plug overmold
    uc = L.usb_center
    rw, rh, rr = c.usb_recess
    body -= prism_y(uc.X, uc.Z, rw, rh, rr, L.usb_face_y, L.D + 1)
    cl = c.usb_open_clear
    body -= prism_y(uc.X, uc.Z, pc.usb_w + 2 * cl, pc.usb_h + 2 * cl, 1.2 + cl, L.D - c.wall - 1, L.D + 1)
    return body


# -------------------------------------------------------------------- base
@lru_cache(maxsize=None)
def base(L: Layout, with_pico: bool = True, with_centre: bool = True) -> Part:
    """``with_centre``: the edition's mount (joystick posts; nothing for the rotary edition)."""
    p = L.p
    c, tol, fas = p.case, p.tol, p.fas
    body = prism(outline(L), 0, c.base_t)
    body = chamfer(outer_edges_of_face(body, Vector(0, 0, -1)), c.base_chamfer)
    body = chamfer(outer_edges_of_face(body, Vector(0, 0, 1)), 0.5)

    # locating lip inside the frame, interrupted at the bosses and the USB end
    ring = outline(L, c.wall + tol.fit) - outline(L, c.wall + tol.fit + c.base_lip_t)
    lip = prism(ring, c.base_t - 0.5, c.base_t + c.base_lip_h)
    for _, x, y in L.bosses:
        lip -= Pos(x, y, 0) * Cylinder(fas.boss_d / 2 + 1.0, BIG, align=CXY)
    lip -= box(L.pico_x - 13, L.pico_x + 13, L.D - 12, L.D, 0, 20)
    body += lip

    # Pico 2 cradle (two standoffs, centre rail, forward stop), LED stick posts, centre-control mount
    if with_pico:
        body = _pico_cradle(L, body)
    body = _led_posts(L, body)
    if with_centre:
        body = L.ed.base_features(L, body)

    # countersunk case screws
    csk_h = (fas.m3_csk_d - fas.m3_clear_d) / 2
    for _, x, y in L.bosses:
        body -= Pos(x, y, -1) * Cylinder(fas.m3_clear_d / 2, c.base_t + 2, align=CXY)
        body -= Pos(x, y, -0.01) * Cone(fas.m3_csk_d / 2, fas.m3_clear_d / 2, csk_h, align=CXY)

    # rubber feet recesses
    fi = c.feet_inset
    for x, y in ((fi, fi), (L.W - fi, fi), (fi, L.D - fi), (L.W - fi, L.D - fi)):
        body -= Pos(x, y, -0.1) * Cylinder(c.feet_d / 2, c.feet_depth + 0.1, align=CXY)
    return body


def _pico_cradle(L: Layout, body: Part) -> Part:
    """Standoffs at the two debug-end holes, a centre rail and a forward stop.
    The USB end is held by the receptacle sitting in the frame's port."""
    p = L.p
    c, fas, pc = p.case, p.fas, p.pico
    ploc = L.loc_pico
    rail = box(14.0, 42.0, -2.5, 2.5, -pc.standoff_h - 0.5, 0)
    stop = box(pc.length + 0.3, pc.length + 2.8, -4.0, 4.0, -pc.standoff_h - 0.5, pc.t + 1.0)
    body += ploc * (rail + stop)
    for sy in (-1, 1):
        hx, hy = pc.length - pc.hole_from_end, sy * pc.hole_spacing / 2
        body += ploc * cyl(hx, hy, -pc.standoff_h - 0.5, 0, 4.2)  # clears pins 20/21 joints
    for sy in (-1, 1):
        hx, hy = pc.length - pc.hole_from_end, sy * pc.hole_spacing / 2
        wpt = (ploc * Pos(hx, hy, 0)).position
        body -= Pos(wpt.X, wpt.Y, 0.8) * Cylinder(fas.m2_pilot_d / 2, wpt.Z, align=CXY)
    return body


def _led_posts(L: Layout, body: Part) -> Part:
    """Two posts per LED stick at its mounting holes, M2 self-tapping from above."""
    c, fas = L.p.case, L.p.fas
    top = c.base_t + c.led_post_h
    for q in L.led_holes:
        body += Pos(q.X, q.Y, c.base_t - 0.5) * Cylinder(c.led_post_d / 2, top - c.base_t + 0.5, align=CXY)
    for q in L.led_holes:
        body -= Pos(q.X, q.Y, 0.8) * Cylinder(fas.m2_pilot_d / 2, top, align=CXY)
    return body


# --------------------------------------------------------------- bar spine
@lru_cache(maxsize=None)
def bar_spine_local(L: Layout) -> Part:
    """Clamp for the bar display, in the bar module frame (z=0 = glass top).

    Screwed through the module's two holes with M2x8, it arches over the
    FPC/ZIF/components and presses the PCB near the header end.
    """
    p = L.p
    b, fas = p.bar, p.fas
    zb = -(b.lcm_t + b.tape_t + b.pcb_t)  # PCB back
    z0 = zb - 6.0  # flat print face
    x_pad0, x_pad1 = b.pcb_l - b.free_back_zone[1], b.pcb_l - b.free_back_zone[0]
    cross = box(1.0, 4.8, -9.6, 9.6, z0, zb)
    beam = box(4.0, x_pad0 + 1.0, -4.0, 4.0, z0, zb - b.back_h - 1.0)
    pad = box(x_pad0, x_pad1, -6.0, 6.0, z0, zb + 0.2)
    spine = cross + beam + pad
    for sy in (-1, 1):
        spine -= cyl(b.hole_x, sy * b.hole_y, z0 - 1, zb + 1, fas.m2_clear_d)
        spine -= cyl(b.hole_x, sy * b.hole_y, z0 - 1, z0 + 2.5, 4.2)
    return spine


@lru_cache(maxsize=None)
def bar_spine(L: Layout) -> Part:
    return L.loc_bar * bar_spine_local(L)


# -------------------------------------------------------------------- knob
@lru_cache(maxsize=None)
# ------------------------------------------------------------- print poses
def print_pose(name: str, part: Part, L: Layout) -> Part:
    """Rotate/translate a world-space part onto the build plate (z >= 0, centred)."""
    if name == "top_plate" or name.startswith("fit_plate"):
        part = Rot(180 - L.p.case.tilt_deg, 0, 0) * part
    elif name == "bar_spine":
        part = bar_spine_local(L)
    else:
        part = L.ed.print_pose(name, part, L) or part
    bb = part.bounding_box()
    return Pos(-(bb.min.X + bb.max.X) / 2, -(bb.min.Y + bb.max.Y) / 2, -bb.min.Z) * part


def all_parts(L: Layout) -> dict[str, Part]:
    out = {
        "top_plate": top_plate(L),
        "frame": frame(L),
        "base": base(L),
        "bar_spine": bar_spine(L),
    }
    out.update(L.ed.parts(L))
    return out
