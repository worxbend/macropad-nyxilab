"""Small, quick prints that verify the critical fits before the full case.

fit_switch       3 MX cut-outs (13.9 / 14.0 / 14.1) in the real plate section
fit_plate_d19    top-plate patch around the 1.9" display (window, pockets, pilots)
fit_plate_bar    top-plate patch around the 2.25" bar display
fit_usb          back wall + Pico cradle (USB-C port alignment, standoffs)
fit_led          base strip under one LED stick with its two posts (hole pitch check)

The edition adds its own (``Edition.fit_tests``): the joystick opening and posts,
or the encoder mount.
"""

from __future__ import annotations

from build123d import Align, FontStyle, Part, Pos, Rot, Text, extrude

from . import parts
from .components import box
from .layout import Layout


def fit_switch(L: Layout) -> Part:
    """Flat coupon, modelled top-up; printed skin-down like the plate."""
    sw, T = L.p.sw, L.p.case.plate_t
    sizes = (13.9, 14.0, 14.1)
    w, d = len(sizes) * sw.pitch + 4, sw.pitch + 8
    body = box(0, w, 0, d, 0, T)
    for i, hole in enumerate(sizes):
        cx, cy = 2 + sw.pitch * (i + 0.5), 4 + sw.pitch / 2
        body -= box(cx - hole / 2, cx + hole / 2, cy - hole / 2, cy + hole / 2, -1, T + 1)
        body -= box(cx - sw.pocket_u / 2, cx + sw.pocket_u / 2, cy - sw.pocket_v / 2, cy + sw.pocket_v / 2, -1,
                    T - sw.skin)
        label = Text(f"{hole:.1f}", font_size=3.0, font_style=FontStyle.BOLD,
                     align=(Align.CENTER, Align.CENTER))
        body -= Pos(cx, 2.0, T - 0.4) * extrude(label, amount=1)
    return Rot(180, 0, 0) * body


def plate_patch(L: Layout, region: Part) -> Part:
    """A piece of the real top plate, in print orientation (editions use this too)."""
    return parts.print_pose("fit_plate", parts.top_plate(L) & region, L)


def base_patch(L: Layout, region: Part, with_pico: bool = False) -> Part:
    """A piece of the real base, centred on the bed."""
    coupon = parts.base(L, with_pico=with_pico) & region
    bb = coupon.bounding_box()
    return Pos(-(bb.min.X + bb.max.X) / 2, -(bb.min.Y + bb.max.Y) / 2, -bb.min.Z) * coupon


def fit_plate_d19(L: Layout) -> Part:
    region = L.ploc * box(L.u_c - 36, L.u_c + 36, L.v_w - 18, L.v_w + 18, -L.p.case.plate_t - 4, 2)
    return plate_patch(L, region)


def fit_plate_bar(L: Layout) -> Part:
    b = L.p.bar
    region = L.loc_bar * box(-3, b.pcb_l + 3, -b.pcb_w / 2 - 3.5, b.pcb_w / 2 + 3.5, -8, 4)
    return plate_patch(L, region)


def fit_usb(L: Layout) -> Part:
    x0, x1 = L.pico_x - 16, L.pico_x + 16
    y0 = L.pico_edge_y - L.p.pico.length - 3.5
    region = box(x0, x1, y0, L.D + 1, -1, L.pico_z + 8)
    coupon = (parts.frame(L) + parts.base(L, with_centre=False)) & region
    bb = coupon.bounding_box()
    return Pos(-(bb.min.X + bb.max.X) / 2, -(bb.min.Y + bb.max.Y) / 2, -bb.min.Z) * coupon


def fit_led(L: Layout) -> Part:
    """Floor strip under the right LED stick with its two posts: lay the stick on, check the holes."""
    s = L.p.led
    q = L.led_locs["led R"].position
    region = box(q.X - s.pcb_w / 2 - 3, q.X + s.pcb_w / 2 + 3, q.Y - s.pcb_l / 2 - 2, q.Y + s.pcb_l / 2 + 2, -1, 20)
    return base_patch(L, region)


def for_layout(L: Layout) -> dict:
    """All coupons of this edition, in printing order."""
    out = {"fit_switch": fit_switch, "fit_plate_d19": fit_plate_d19, "fit_plate_bar": fit_plate_bar}
    out.update(L.ed.fit_tests(L))
    out["fit_usb"] = fit_usb
    out["fit_led"] = fit_led
    return out
