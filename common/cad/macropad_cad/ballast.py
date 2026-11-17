"""Drop-in weight for an already-printed case.

One solid per edition. It sits on the base, under the switch wiring, and leaves
the Pico bay, both LED columns and the centre module open so it can drop in
after those parts are mounted. Upward trenches hold the hand-wired bundles.
Existing case dimensions are not changed.
"""

from __future__ import annotations

from functools import lru_cache

from build123d import Align, Cylinder, Part, Pos

from .components import box, placed_keepouts
from .layout import Layout
from .parts import BIG, outline, plate_band, prism

FLOOR_GAP = 0.30  # mm above the base top face
WALL_CLEAR = 0.40  # mm inside the base lip
WIRE_GAP = 1.2  # mm along the plate normal, below the switch pin keep-out
BOSS_CLEAR = 0.60  # mm radial around each M3 boss
WELL_CLEAR = 1.5  # mm around a base-mounted footprint
PICO_NOSE = 3.4  # mm past the Pico keep-out, covers the cradle's forward stop
SOLE = 2.0  # mm of plastic left under a trench
GAP_SHIFT = 0.45  # mm; shifted copies stand in for a solid offset
MIN_SOLID_MM3 = 400.0
CEILING = 2.0  # mm of blank above the case, cut back by the wire plane

# World-mm lanes, open upward. The spine meets the Pico bay. The jumper joins
# the stick fronts. The bar lane and its cross carry the bar-display leads.
# The header lane is the 1.9" drop on the Pico's right.
SPINE_HALF = 9.0
SPINE_Y = (16.0, 48.0)
JUMPER_X = (36.0, 124.0)
JUMPER_Y = (22.0, 30.0)
BAR_X = (11.0, 23.0)
BAR_Y = (14.0, 46.0)
CROSS_Y = (36.0, 44.0)
HEADER_DX = (14.0, 32.0)
HEADER_Y = (50.0, 80.0)

HUNG = ("display bar", "display 1.9", "encoder")
_SHIFTS = ((GAP_SHIFT, 0.0, 0.0), (-GAP_SHIFT, 0.0, 0.0), (0.0, GAP_SHIFT, 0.0),
           (0.0, -GAP_SHIFT, 0.0), (0.0, 0.0, -GAP_SHIFT))


def _union(shapes: list[Part]) -> Part:
    body = shapes[0]
    for shape in shapes[1:]:
        body += shape
    return body


def _with_gap(solid: Part) -> Part:
    """Grow a keep-out by GAP_SHIFT on the sides and downward.

    ``Part.offset_3d`` returns a shell, so the clearance is a union of copies.
    """
    return _union([solid, *[(Pos(*shift) * solid) for shift in _SHIFTS]])


def _vertical_well(solid: Part, pad: float, z0: float, z1: float) -> Part:
    bb = solid.bounding_box()
    return box(bb.min.X - pad, bb.max.X + pad, bb.min.Y - pad, bb.max.Y + pad, z0, z1)


def _pico_well(L: Layout, solid: Part, z0: float, z1: float) -> Part:
    """Column through the back edge, so the piece lowers over a seated Pico."""
    bb = solid.bounding_box()
    y1 = max(bb.max.Y, L.D) + WELL_CLEAR
    return box(bb.min.X - WELL_CLEAR, bb.max.X + WELL_CLEAR, bb.min.Y - PICO_NOSE, y1, z0, z1)


def _boss_holes(L: Layout, z0: float, z1: float) -> Part:
    radius = L.p.fas.boss_d / 2 + BOSS_CLEAR
    align = (Align.CENTER, Align.CENTER, Align.MIN)
    holes = [Pos(x, y, z0) * Cylinder(radius, z1 - z0, align=align) for _, x, y in L.bosses]
    return _union(holes)


def _centre_wells(L: Layout, kos: dict[str, Part], z0: float, z1: float) -> list[Part]:
    """Joystick posts and the dome sweep. The rotary knob stays above the plate."""
    if "joystick" not in kos:
        return []
    _, sweep = L.ed.sweep(L)
    return [_vertical_well(kos["joystick"], WELL_CLEAR, z0, z1),
            _vertical_well(sweep, WELL_CLEAR, z0, z1)]


def _hung_modules(kos: dict[str, Part]) -> list[Part]:
    return [_with_gap(kos[name]) for name in HUNG if name in kos]


def _lane_spans(L: Layout) -> tuple[tuple[float, float, float, float], ...]:
    px = L.pico_x
    return (
        (px - SPINE_HALF, px + SPINE_HALF, *SPINE_Y),
        (*JUMPER_X, *JUMPER_Y),
        (*BAR_X, *BAR_Y),
        (BAR_X[0], px, *CROSS_Y),
        (px + HEADER_DX[0], px + HEADER_DX[1], *HEADER_Y),
    )


def _lanes(L: Layout, z0: float, z1: float) -> Part:
    return _union([box(x0, x1, y0, y1, z0, z1) for x0, x1, y0, y1 in _lane_spans(L)])


def _wedge(L: Layout, z0: float) -> Part:
    c, tol, sw = L.p.case, L.p.tol, L.p.sw
    inset = c.wall + tol.fit + c.base_lip_t + WALL_CLEAR
    wire_w = -(sw.body_below + sw.pins_below + sw.wiring_below) - WIRE_GAP
    blank = prism(outline(L, inset), z0, L.H_b + CEILING)
    return blank & plate_band(L, -BIG, wire_w)


def _service_cuts(L: Layout, z0: float, z1: float) -> Part:
    kos = placed_keepouts(L)
    below, above = z0 - 1.0, z1 + 1.0
    cuts = [
        _boss_holes(L, below, above),
        _pico_well(L, kos["pico"], below, above),
        *[_vertical_well(kos[name], WELL_CLEAR, below, above) for name in L.led_locs],
        *_centre_wells(L, kos, below, above),
        *_hung_modules(kos),
        _lanes(L, z0 + SOLE, above),
    ]
    return _union(cuts)


def _keep_chunks(part: Part) -> Part:
    kept = [solid for solid in part.solids() if solid.volume >= MIN_SOLID_MM3]
    if not kept:
        raise ValueError("ballast boolean left nothing printable")
    return _union(kept)


@lru_cache(maxsize=None)
def ballast(L: Layout) -> Part:
    z0 = L.p.case.base_t + FLOOR_GAP
    return _keep_chunks(_wedge(L, z0) - _service_cuts(L, z0, L.H_b + CEILING))
