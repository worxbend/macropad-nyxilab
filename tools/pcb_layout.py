#!/usr/bin/env python3
"""PCB version of the wiring, fitted into the case as it is (the case does not change).

    python tools/pcb_layout.py [edition ...]        (default: both)   -> pcb/<edition>/

Three boards replace the hand wiring, joined by flat cables:
  left, right  switch boards flat against the top plate's underside (plate frame u, v, w in mm):
               the 4 switches of the left column / the 8 of the right block, plus a low JST-SH
               connector for the display (and the encoder) next to them; one 20-pin FPC each
  hat          a board on 2.54 mm headers on top of the Pico 2 (world x, y, z in mm), so the Pico
               and its USB-C port stay where they are; the two FPCs and the LED cable plug in here

Outlines are a generous region minus everything of the case model at the board's height, grown by
CLEAR.  Then every new part (boards, headers, connectors, the flat-cable and pigtail routes) is
checked against the case and against each other like `make cad-check` does, and the drop-in
ballast is cut around everything that stands on the base (ballast_pcb.stl).

Writes per edition: left/right/hat.dxf (Edge.Cuts for KiCad, +y = towards the back),
positions.json (switches with their matrix nets, Pico pins with their GPIOs, every connector),
fit.json (the overlap of every pair checked), plate_preview.svg, hat_preview.svg, ballast_pcb.stl.
Exits non-zero if a switch or a Pico pin is off its board or any pair overlaps.
"""

from __future__ import annotations

import itertools
import json
import re
import struct
import sys
from pathlib import Path

import manifold3d as m3d
import numpy as np
from build123d import Pos

from editions import EDITIONS, REPO, load

from macropad_cad import ballast as ballast_mod  # noqa: E402  (editions puts common/cad on sys.path)
from macropad_cad import checks, components, parts  # noqa: E402
from macropad_cad.layout import Layout  # noqa: E402

OUT = REPO / "pcb"
CLEAR = 0.5  # mm from a board edge to anything in the case
LIMIT = 0.05  # mm3 of overlap a checked pair may have (as in checks.py)
PCB_T = 1.6
SMD = 1.0  # height of the SMD parts (diodes, resistors, the level shifter)
UNDER = 4.6  # switch board: board + the tallest part below it (JST-SH with its plug, 2.9 + margin)
HAT_Z = (9.5, 17.5)  # hat bottom (2.5 mm header spacer on the Pico) .. the tallest part on it
# Through the hat over the Pico's two M2 holes, 3.2 mm from pins 19-22: 4.0 mm keeps 0.3 mm to their pads and passes an
# M2 x 5 button-head screw (ISO 7380, 3.5 mm head, 1.3 mm hex key); a pan head (4.0 mm) does not fit through.
SCREW_HOLE = 4.0
SWITCH_PAD = 7.0  # half the board area a switch's pins and diode need
# MX switch, below its housing: the two contact pins and the centre post (x, y from the switch centre, diameter,
# length) - they come through the board, so nothing may sit under them.  Solder joints add 0.5 mm.
MX_BELOW = ((-3.81, 2.54, 1.5, 3.3), (2.54, 5.08, 1.5, 3.3), (0.0, 0.0, 4.0, 3.0))
# Connector courtyards of the KiCad footprints the boards use (tools/pcb_kicad.py), height from the datasheets.
FPC = (15.2, 7.0, 2.3)  # Hirose FH12-20S-0.5SH body + pads (courtyard 16.2 x 8.0): cable width, depth, height
SH = (6.7, 2.9)  # JST-SH SMxxB-SRSS-TB side entry: depth, height; width = pins + 3.9 mm
SH_PLUG = 4.0  # how far a mated JST-SH plug stands out of its connector
PH = (10.3, 6.0)  # JST-PH S3B-PH-SM4-TB (LED cable): depth, height; width 11.3 mm
# On the encoder / joystick's own right-angle header: a 2.54 mm female header (8.5 mm) with the pigtail soldered to
# its tails, 10.5 mm in all.  A crimped Dupont housing (14 mm) reaches over the left LED stick's front end.
FEMALE = 10.5
DENSITY = 1.24  # PLA, g/cm3
# Pico 2 pins whose GPIO the firmware uses: physical pin -> GP number
PICO_GP = {1: 0, 2: 1, 4: 2, 5: 3, 6: 4, 7: 5, 9: 6, 10: 7, 11: 8, 12: 9, 14: 10, 15: 11, 16: 12, 17: 13,
           19: 14, 20: 15, 21: 16, 22: 17, 24: 18, 25: 19, 26: 20, 27: 21, 29: 22, 31: 26, 32: 27, 34: 28}
POWER = {36: "3V3", 39: "VSYS", 40: "VBUS", 3: "GND", 8: "GND", 13: "GND", 18: "GND", 23: "GND", 28: "GND",
         33: "AGND", 38: "GND"}


# ------------------------------------------------------------------ geometry
def cube(x0, x1, y0, y1, z0, z1) -> m3d.Manifold:
    return m3d.Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def hull(*solids: m3d.Manifold) -> m3d.Manifold:
    return m3d.Manifold.batch_hull(list(solids))


def rect(x0, x1, y0, y1) -> m3d.CrossSection:
    return m3d.CrossSection.square((x1 - x0, y1 - y0)).translate((x0, y0))


class Frames:
    """The plate frame (u along the width, v up the slope, w out of the top surface) and the world."""

    def __init__(self, L: Layout):
        c, s, h = L.cos, L.sin, L.H_f
        self.to_world = [[1, 0, 0, 0], [0, c, -s, 0], [0, s, c, h]]
        self.to_plate = [[1, 0, 0, 0], [0, c, s, -s * h], [0, -s, c, -c * h]]

    def plate(self, u0, u1, v0, v1, w0, w1) -> m3d.Manifold:
        return cube(u0, u1, v0, v1, w0, w1).transform(self.to_world)

    def prism(self, cs: m3d.CrossSection, w0: float, w1: float) -> m3d.Manifold:
        return m3d.Manifold.extrude(cs, w1 - w0).translate((0, 0, w0)).transform(self.to_world)


def shadow(solids: list[m3d.Manifold], to_board, slab: m3d.Manifold) -> m3d.CrossSection:
    """What the solids occupy inside `slab` (board frame), seen along the board normal."""
    out = m3d.CrossSection()
    for s in solids:
        cut = (s.transform(to_board) if to_board else s) ^ slab
        if not cut.is_empty():
            out = out + cut.project()
    return out


def free_piece(region: m3d.CrossSection, blocked: m3d.CrossSection, anchors: list) -> m3d.CrossSection:
    free = region - blocked.offset(CLEAR, m3d.JoinType.Round)
    pieces = [p for p in free.decompose() if any(inside(p, a) for a in anchors)]
    if len(pieces) != 1:
        raise SystemExit(f"{len(pieces)} free pieces hold the board's anchors, expected 1")
    return pieces[0]


def inside(cs: m3d.CrossSection, pt, half: float = 0.01) -> bool:
    return (rect(pt[0] - half, pt[0] + half, pt[1] - half, pt[1] + half) - cs).area() < 1e-9


def fits(cs: m3d.CrossSection, r: m3d.CrossSection) -> bool:
    return (r - cs).area() < 1e-6


def edge_slot(cs: m3d.CrossSection, depth: float, v0: float, v1: float, side: int) -> tuple[float, float]:
    """The u range of a part `depth` deep, spanning v0..v1, pushed against the board's right (side +1) or left (-1)
    edge as it runs at those v, CLEAR inside it."""
    x0, _, x1, _ = cs.bounds()
    steps = np.arange(x1 - CLEAR, x0 + depth, -0.05) if side > 0 else np.arange(x0 + CLEAR, x1 - depth, 0.05)
    for e in steps:
        u0, u1 = (e - depth, e) if side > 0 else (e, e + depth)
        if fits(cs.offset(-CLEAR, m3d.JoinType.Miter), rect(u0, u1, v0, v1)):
            return float(u0), float(u1)
    raise SystemExit(f"no room for a {depth} mm deep part at v {v0:.1f}..{v1:.1f}")


def place(cs: m3d.CrossSection, w: float, h: float, target, avoid: m3d.CrossSection) -> tuple[float, float]:
    """The centre nearest `target` where a w x h rectangle lies on `cs` clear of `avoid`."""
    x0, y0, x1, y1 = cs.bounds()
    best = None
    for x in np.arange(x0 + w / 2, x1 - w / 2, 0.5):
        for y in np.arange(y0 + h / 2, y1 - h / 2, 0.5):
            r = rect(x - w / 2, x + w / 2, y - h / 2, y + h / 2)
            if not fits(cs, r) or (r ^ avoid).area() > 1e-6:
                continue
            d = (x - target[0]) ** 2 + (y - target[1]) ** 2
            if best is None or d < best[0]:
                best = (d, float(x), float(y))
    if best is None:
        raise SystemExit(f"no room for a {w} x {h} mm part on the board")
    return best[1], best[2]


# ------------------------------------------------------------- the case model
def case_solids(L: Layout) -> dict[str, m3d.Manifold]:
    """The case as the PCB version has it: no hand wiring, no old ballast (replaced), the Pico as built."""
    out = {f"case {n}": checks.to_manifold(s) for n, s in parts.all_parts(L).items() if n != "ballast"}
    for n, s in components.placed_keepouts(L).items():
        if not n.startswith(("switch", "pico")):
            out[f"ko {n}"] = checks.to_manifold(s)
    for n, s in components.pico_model(L.p).items():
        out[f"pico {n}"] = checks.to_manifold(L.loc_pico * s)
    name, sweep = L.ed.sweep(L)
    out[f"sweep {name}"] = checks.to_manifold(sweep)
    return out


def matrix_pins(name: str) -> tuple[list[int], list[int]]:
    text = (REPO / name / "firmware" / "include" / "config.h").read_text()
    grab = lambda key: [int(v) for v in re.search(key + r"\[[^]]*\]\s*=\s*\{([^}]*)\}", text).group(1).split(",")]  # noqa: E731
    return grab("ROW_PINS"), grab("COL_PINS")


def pico_pins(L: Layout) -> list[dict]:
    pc = L.p.pico
    pins = []
    for n in range(1, 41):
        i, side = (n - 1, 1) if n <= 20 else (40 - n, -1)
        q = (L.loc_pico * Pos(pc.first_pin + i * pc.pin_pitch, side * pc.row_spacing / 2, 0)).position
        net = f"GP{PICO_GP[n]}" if n in PICO_GP else POWER.get(n, f"pin {n}")
        pins.append({"pin": n, "net": net, "x": round(q.X, 3), "y": round(q.Y, 3)})
    return pins


# ---------------------------------------------------------------- the design
class Design:
    def __init__(self, name: str):
        self.name = name
        self.L = L = Layout(load(name))
        self.F = Frames(L)
        self.case = case_solids(L)
        self.parts: dict[str, m3d.Manifold] = {}  # every new solid, checked
        self.allowed: set[frozenset] = set()  # pairs that touch or overlap by design
        self.connectors: dict[str, dict] = {}
        self.top = -L.p.sw.body_below  # switch housings end flush with the plate's underside
        self.switch_boards()
        self.display_links()
        self.centre_link()
        self.hat_board()
        self.flat_cables()
        self.led_cables()

    def add(self, name: str, solid: m3d.Manifold, *touches: str) -> None:
        self.parts[name] = solid
        self.allowed |= {frozenset((name, t)) for t in touches}

    # -- switch boards ------------------------------------------------------
    def switch_boards(self):
        L, F = self.L, self.F
        slab = cube(-50, L.W + 50, -50, L.V + 50, self.top - UNDER, self.top - 0.05)
        self.plate_shadow = shadow(list(self.case.values()), F.to_plate, slab)
        self.boards = {}
        for name, cols, (u0, u1) in (("left", (0,), (L.u_L - 11, L.u_L + 12)), ("right", (1, 2), (L.u_R1 - 12, L.W))):
            keys = [k for k in L.keys if k.col in cols]
            cs = free_piece(rect(u0, u1, 0, L.V), self.plate_shadow, [(k.u, k.v) for k in keys])
            for k in keys:
                pad = rect(k.u - SWITCH_PAD, k.u + SWITCH_PAD, k.v - SWITCH_PAD, k.v + SWITCH_PAD)
                if not fits(cs, pad):
                    raise SystemExit(f"{self.name}: a switch at u {k.u:.1f} v {k.v:.1f} is off the {name} board")
            self.boards[name] = (cs, keys)
            self.add(f"board {name}", F.prism(cs, self.top - PCB_T - SMD, self.top))
            for k in keys:  # after the outline: the pins are inside the board, not around it
                pins = m3d.Manifold.batch_boolean(
                    [m3d.Manifold.cylinder(length + 0.5, d / 2, circular_segments=24)
                     .translate((k.u + x, k.v + y, self.top - length - 0.5)) for x, y, d, length in MX_BELOW],
                    m3d.OpType.Add).transform(F.to_world)
                pin_name = f"pins R{len(L.rows) - 1 - k.row}C{k.col}"
                self.case[pin_name] = pins
                self.allowed.add(frozenset((f"board {name}", pin_name)))

    def pin_shadow(self, side: str) -> m3d.CrossSection:
        """The switch pins' solder joints (2.2 mm pads) and centre posts of a board seen from below, plus 0.3 mm."""
        out = m3d.CrossSection()
        for k in self.boards[side][1]:
            for x, y, d, _ in MX_BELOW:
                r = 1.1 + 0.3 if d < 3 else d / 2 + 0.3
                out = out + m3d.CrossSection.circle(r, 24).translate((k.u + x, k.v + y))
        return out

    def edge_slot_clear_of_pins(self, cs: m3d.CrossSection, side: str, preferred: float, sign: int):
        pins = self.pin_shadow(side)
        _, y0, _, y1 = cs.bounds()
        for vc in sorted(np.arange(y0 + FPC[0] / 2, y1 - FPC[0] / 2, 0.25), key=lambda v: abs(v - preferred)):
            try:
                u0, u1 = edge_slot(cs, FPC[1], vc - FPC[0] / 2, vc + FPC[0] / 2, sign)
            except SystemExit:
                continue
            if (rect(u0, u1, vc - FPC[0] / 2, vc + FPC[0] / 2) ^ pins).area() < 1e-6:
                return float(vc), (u0, u1)
        raise SystemExit(f"no slot for the {side} FPC along the board's edge clear of the switch pins")

    def board_connector(self, board: str, label: str, u0, u1, v0, v1, kind: str, cable_from: str) -> m3d.Manifold:
        """A connector on a switch board's underside (plate frame), recorded for positions.json."""
        h = FPC[2] if kind == "FPC" else SH[1]
        w0 = self.top - PCB_T - h
        solid = self.F.plate(u0, u1, v0, v1, w0, self.top - PCB_T)
        self.connectors[label] = {"board": board, "kind": kind, "u": [round(u0, 2), round(u1, 2)],
                                  "v": [round(v0, 2), round(v1, 2)], "side": "underside", "cable_from": cable_from}
        self.add(f"{label}", solid, f"board {board}")
        return solid

    # -- displays and the centre control -------------------------------------
    def display_links(self):
        """Each display: an 8-wire JST-SH pigtail soldered into its header holes (on the bench), its plug in a
        connector on the underside of the switch board next to it.  A 2.54 mm Dupont plug is 14 mm tall: under
        the main display it reaches the hat, under the bar display the floor."""
        F, L, p = self.F, self.L, self.L.p
        w_sh = self.top - PCB_T - SH[1]
        # main display: header holes at its right end, the right board's left edge 2.5 mm further
        d = p.d19
        zb = -p.case.disp_skin - (d.lcm_t + d.tape_t) - d.pcb_t  # module PCB underside (plate frame w)
        h0 = (L.ploc.inverse() * L.loc_d19 * Pos(d.header_x, -8.89, 0)).position
        h1 = (L.ploc.inverse() * L.loc_d19 * Pos(d.header_x, 8.89, 0)).position
        u_hdr, va, vb = h0.X, min(h0.Y, h1.Y), max(h0.Y, h1.Y)
        u_edge = self.boards["right"][0].bounds()[0]
        vc = (va + vb) / 2
        self.board_connector("right", "SH8 main display", u_edge + CLEAR, u_edge + CLEAR + SH[0],
                             vc - 5.95, vc + 5.95, "JST-SH 8", "-u")
        plug = F.plate(u_edge + CLEAR - SH_PLUG, u_edge + CLEAR, vc - 5.5, vc + 5.5, w_sh, self.top - PCB_T)
        joints = F.plate(u_hdr - 1.2, u_hdr + 1.2, va - 1.3, vb + 1.3, zb - 1.0, zb)
        self.add("pigtail main display", hull(plug, joints), "SH8 main display", "ko display 1.9", "board right")
        # bar display: header holes across its front end, the left board's left edge just right of them
        b = p.bar
        zb = -p.case.disp_skin - (b.lcm_t + b.tape_t) - b.pcb_t
        hx = b.pcb_l - b.header_x_from_end
        h0 = (L.ploc.inverse() * L.loc_bar * Pos(hx, -8.89, 0)).position
        h1 = (L.ploc.inverse() * L.loc_bar * Pos(hx, 8.89, 0)).position
        ua, ub, v_hdr = min(h0.X, h1.X), max(h0.X, h1.X), h0.Y
        # the bar spine clamps the module right behind its header: the pigtail leaves towards the front
        left = self.boards["left"][0]
        u_edge, v0 = left.bounds()[0], left.bounds()[1]
        v_sh = (v0 + CLEAR, v0 + CLEAR + 11.9)
        self.board_connector("left", "SH8 bar display", u_edge + CLEAR, u_edge + CLEAR + SH[0], *v_sh, "JST-SH 8", "-u")
        plug = F.plate(u_edge + CLEAR - SH_PLUG, u_edge + CLEAR, *v_sh, w_sh, self.top - PCB_T)
        joints = F.plate(ua - 1.3, ub + 1.3, v_hdr - 1.0, v_hdr + 1.0, zb - 1.0, zb)
        self.add("pigtail bar display", hull(plug, joints), "SH8 bar display", "ko display bar", "board left")

    def centre_link(self):
        """The encoder / joystick module keeps its own right-angle header: a Dupont plug on it, wires to a JST-SH."""
        L = self.L
        comp = L.ed.components(L)
        cname = next(n for n, c in comp.items() if "header" in c)
        header = checks.to_manifold(comp[cname]["header"])
        on_plate = cname == "encoder"  # the KY-040 hangs from the plate, the KY-023 stands on the base
        x0, y0, z0, x1, y1, z1 = (header.transform(self.F.to_plate) if on_plate else header).bounding_box()
        housing = cube(x1 - 2.5 - FEMALE, x1 - 2.5, y0, y1, z0, z1)  # from the header's plastic outwards (-x/-u)
        end = np.array([x1 - 2.5 - FEMALE, (y0 + y1) / 2, (z0 + z1) / 2])
        if on_plate:
            housing = housing.transform(self.F.to_world)
            end = np.array(self.F.to_world) @ np.append(end, 1.0)
        self.add(f"female {cname}", housing, f"ko {cname}")
        self.centre = (cname, housing, tuple(float(v) for v in end))

    # -- the hat ---------------------------------------------------------------
    def hat_board(self):
        L = self.L
        on_plate = list(self.parts.values())  # the switch boards, their connectors, the pigtails, the centre plug
        slab = cube(-50, L.W + 50, -50, L.D + 50, *HAT_Z)
        solids = list(self.case.values()) + on_plate
        self.hat_shadow = shadow(solids, None, slab)
        self.pins = pins = pico_pins(L)
        xs, ys = [p["x"] for p in pins], [p["y"] for p in pins]
        region = rect(min(xs) - 25, max(xs) + 25, min(ys) - 20, L.D)
        self.hat = free_piece(region, self.hat_shadow, [(sum(xs) / 40, sum(ys) / 40)])
        pc = L.p.pico  # the Pico is screwed down through the hat, which is soldered on before it goes in
        self.screw_holes = []
        for side in (-1, 1):
            q = (L.loc_pico * Pos(pc.length - pc.hole_from_end, side * pc.hole_spacing / 2, 0)).position
            self.screw_holes.append((round(q.X, 3), round(q.Y, 3)))
            self.hat = self.hat - m3d.CrossSection.circle(SCREW_HOLE / 2, 48).translate((q.X, q.Y))
        if len(self.hat.decompose()) != 1:
            raise SystemExit(f"{self.name}: the screw holes split the hat")
        for p in pins:
            if p["net"] != f"pin {p['pin']}" and not inside(self.hat, (p["x"], p["y"]), 1.0):
                raise SystemExit(f"{self.name}: Pico pin {p['pin']} ({p['net']}) is not on the hat")
        z0 = HAT_Z[0]
        self.add("hat", m3d.Manifold.extrude(self.hat, PCB_T + SMD).translate((0, 0, z0)))
        pc = L.p.pico
        pico_top = L.pico_z + pc.t
        rows = {}
        for p in pins:
            rows.setdefault(p["x"], []).append(p["y"])
        spacers = [cube(x - 1.27, x + 1.27, min(y) - 1.27, max(y) + 1.27, pico_top + pc.parts_h, z0)
                   for x, y in rows.items()]
        self.add("hat headers", m3d.Manifold.batch_boolean(spacers, m3d.OpType.Add), "hat",
                 "pico pcb", "pico chips", "pico usb")
        self.hat_used = m3d.CrossSection()  # connector footprints already placed on the hat

    def hat_connector(self, label: str, w: float, d: float, h: float, target, facing: str) -> tuple:
        """Place a connector on the hat's top nearest `target`; `facing` is the side its cable enters from."""
        along_x = facing in ("-y", "+y")
        sx, sy = (w, d) if along_x else (d, w)
        cx, cy = place(self.hat, sx + 1.0, sy + 1.0, target, self.hat_used)
        z0 = HAT_Z[0] + PCB_T
        solid = cube(cx - sx / 2, cx + sx / 2, cy - sy / 2, cy + sy / 2, z0, z0 + h)
        self.hat_used = self.hat_used + rect(cx - sx / 2 - 0.5, cx + sx / 2 + 0.5, cy - sy / 2 - 0.5, cy + sy / 2 + 0.5)
        self.connectors[label] = {"board": "hat", "x": [round(cx - sx / 2, 2), round(cx + sx / 2, 2)],
                                  "y": [round(cy - sy / 2, 2), round(cy + sy / 2, 2)], "cable_from": facing}
        self.add(label, solid, "hat")
        mouth = {"-x": (cx - sx / 2, None), "+x": (cx + sx / 2, None), "-y": (None, cy - sy / 2),
                 "+y": (None, cy + sy / 2)}[facing]
        if mouth[0] is not None:
            face = cube(mouth[0] - 0.01, mouth[0] + 0.01, cy - sy / 2, cy + sy / 2, z0 + 0.5, z0 + h - 0.5)
        else:
            face = cube(cx - sx / 2, cx + sx / 2, mouth[1] - 0.01, mouth[1] + 0.01, z0 + 0.5, z0 + h - 0.5)
        return solid, face

    # -- cables -----------------------------------------------------------------
    def flat_cables(self):
        """Each switch board -> hat: a 20-pin 0.5 mm FFC from an FPC connector at the board's inner edge."""
        F = self.F
        cname, housing, end = self.centre
        if cname == "joystick":  # it stands on the base: its wires go to the front of the hat, placed first
            solid, face = self.hat_connector("SH5 joystick", 8.9, SH[0], SH[1], (end[0] + 7.0, end[1] + 9.0), "-y")
            tip = cube(end[0] - 0.5, end[0], end[1] - 6.0, end[1] + 6.0, end[2] - 1.2, end[2] + 1.2)
            self.add("wires joystick", hull(face.translate((0, -2.0, 0)), tip), "SH5 joystick", f"female {cname}",
                     "hat", "ko joystick")
        for side, sign in (("left", 1), ("right", -1)):
            cs = self.boards[side][0]
            # preferred: between the pins of two switch rows, rows 2-3 from the front on the left, rows 1-2 on the right
            # (below the main display and its pigtail); taken: the nearest slot along the real edge clear of all pins
            pin2 = sorted(k.v + 2.54 for k in self.L.keys if k.col == 0)
            gap = 1 if sign > 0 else 0
            vc, (u0, u1) = self.edge_slot_clear_of_pins(cs, side, (pin2[gap] + pin2[gap + 1]) / 2, sign)
            edge = u1 + CLEAR if sign > 0 else u0 - CLEAR
            self.board_connector(side, f"FPC20 {side}", u0, u1, vc - FPC[0] / 2, vc + FPC[0] / 2, "FPC",
                                 "+u" if sign > 0 else "-u")
            w_mid = self.top - PCB_T - FPC[2] / 2
            mouth_a = F.plate(edge - 0.01, edge + 0.01, vc - FPC[0] / 2, vc + FPC[0] / 2, w_mid - 0.5, w_mid + 0.5)
            out_a = mouth_a.translate((sign * 3.0, 0, 0))
            wa = np.array(self.F.to_world) @ np.array([edge, vc, w_mid, 1.0])
            _, face = self.hat_connector(f"FPC20 hat {side}", FPC[0], FPC[1], FPC[2], (wa[0] + sign * 6, wa[1]),
                                         "-x" if sign > 0 else "+x")
            out_b = face.translate((-sign * 3.0, 0, 0))
            self.add(f"FFC {side}", hull(mouth_a, out_a, face, out_b), f"FPC20 {side}", f"FPC20 hat {side}",
                     f"board {side}", "hat")
        if cname == "encoder":  # the KY-040 hangs from the plate: its wires go to the left board
            cs = self.boards["left"][0]
            u_edge = cs.bounds()[2]
            v_end = (np.array(F.to_plate) @ np.array([*end, 1.0]))[1]
            u0 = u_edge - CLEAR - SH_PLUG - SH[0]
            front = min(self.L.rows)  # just behind pin 1 of the front switch (5.08 mm behind its centre)
            vc = max(v_end, front + 5.08 + 0.75 + 0.6 + 4.45)
            self.board_connector("left", "SH5 encoder", u0, u0 + SH[0], vc - 4.45, vc + 4.45, "JST-SH 5", "+u")
            plug = F.plate(u0 + SH[0], u0 + SH[0] + SH_PLUG, vc - 4.0, vc + 4.0,
                           self.top - PCB_T - SH[1], self.top - PCB_T)
            tip = cube(end[0] - 0.5, end[0], end[1] - 6.0, end[1] + 6.0, end[2] - 1.2, end[2] + 1.2)
            self.add("wires encoder", hull(plug, tip), "SH5 encoder", f"female {cname}", "board left", "ko encoder")

    def led_cables(self):
        """Hat -> right stick (DIN at its back end), right stick -> left stick along their front ends."""
        L = self.L
        s = L.p.led
        right, left = L.led_locs["led R"].position, L.led_locs["led L"].position
        back_end = right.Y + s.pcb_l / 2
        solid, face = self.hat_connector("PH3 LED", 11.3, PH[0], PH[1], (right.X - 9, back_end - 4), "+x")
        pads = cube(right.X - 4.5, right.X + 4.5, back_end - s.pad_zone, back_end + 1.0,
                    L.p.case.base_t + 0.8, L.p.case.base_t + L.p.case.led_post_h)
        self.add("pigtail LED", hull(face.translate((2.0, 0, 0)), pads), "PH3 LED", "hat", "ko led R")
        front = right.Y - s.pcb_l / 2
        chain = cube(left.X - 1.5, right.X + 1.5, front - s.wire_out, front,
                     L.p.case.base_t + 0.3, L.p.case.base_t + L.p.case.led_post_h - 0.3)
        self.add("chain LED", chain, "ko led R", "ko led L")

    # -- checks -----------------------------------------------------------------
    def check(self) -> list[dict]:
        results = []
        for (an, am), (bn, bm) in itertools.chain(itertools.product(self.parts.items(), self.case.items()),
                                                  itertools.combinations(self.parts.items(), 2)):
            v = checks.overlap(am, bm)
            ok = v <= LIMIT or frozenset((an, bn)) in self.allowed
            results.append({"a": an, "b": bn, "overlap_mm3": round(v, 3), "ok": ok,
                            "by_design": frozenset((an, bn)) in self.allowed})
        return results

    def ballast(self) -> tuple[m3d.Manifold, float, float]:
        """The old ballast minus a vertical prism over everything on the base (it drops in from above)."""
        old = checks.to_manifold(ballast_mod.ballast(self.L))
        base_side = [m for n, m in self.parts.items() if not n.startswith(("board", "SH8", "pigtail main",
                                                                           "pigtail bar"))]
        foot = m3d.CrossSection()
        for m in base_side:
            foot = foot + m.project()
        cut = m3d.Manifold.extrude(foot.offset(1.5, m3d.JoinType.Round), 80.0).translate((0, 0, -10.0))
        new = old - cut
        keep = [p for p in new.decompose() if p.volume() >= ballast_mod.MIN_SOLID_MM3]
        new = m3d.Manifold.batch_boolean(keep, m3d.OpType.Add) if keep else m3d.Manifold()
        return new, old.volume(), new.volume()


# ------------------------------------------------------------------- outputs
def polygons(cs: m3d.CrossSection) -> list:
    return [[(float(x), float(y)) for x, y in poly] for poly in cs.to_polygons()]


def write_dxf(path: Path, cs: m3d.CrossSection) -> None:
    rows = ["0", "SECTION", "2", "HEADER", "9", "$INSUNITS", "70", "4", "0", "ENDSEC", "0", "SECTION", "2", "ENTITIES"]
    for poly in polygons(cs):
        rows += ["0", "POLYLINE", "8", "Edge.Cuts", "66", "1", "70", "1"]
        for x, y in poly:
            rows += ["0", "VERTEX", "8", "Edge.Cuts", "10", f"{x:.4f}", "20", f"{y:.4f}"]
        rows += ["0", "SEQEND"]
    path.write_text("\n".join(rows + ["0", "ENDSEC", "0", "EOF"]) + "\n")


def write_stl(path: Path, solid: m3d.Manifold) -> None:
    mesh = solid.to_mesh()
    v, t = np.asarray(mesh.vert_properties)[:, :3], np.asarray(mesh.tri_verts)
    with path.open("wb") as f:
        f.write(b"nyxilab ballast for the PCB version".ljust(80, b" ") + struct.pack("<I", len(t)))
        for a, b, c in v[t]:
            n = np.cross(b - a, c - a)
            n = n / (np.linalg.norm(n) or 1.0)
            f.write(struct.pack("<12fH", *n, *a, *b, *c, 0))


def svg_paths(cs, fill, stroke, flip) -> str:
    return "\n".join(f'<path d="M{" L".join(f"{x:.2f},{flip - y:.2f}" for x, y in poly)} Z" fill="{fill}" '
                     f'stroke="{stroke}" stroke-width="0.3" fill-rule="evenodd"/>' for poly in polygons(cs))


def write_svg(path: Path, title: str, w: float, h: float, layers: list[str]) -> None:
    path.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-5 -5 {w + 10} {h + 10}" '
                    f'width="{(w + 10) * 6:.0f}" height="{(h + 10) * 6:.0f}">\n<rect x="-5" y="-5" width="{w + 10}" '
                    f'height="{h + 10}" fill="#fafaf7"/>\n<text x="-3" y="0" font-size="3.5" font-family="sans-serif">'
                    f'{title}</text>\n' + "\n".join(layers) + "\n</svg>\n")


def bounds(cs) -> dict:
    x0, y0, x1, y1 = cs.bounds()
    return {"min": [round(x0, 2), round(y0, 2)], "max": [round(x1, 2), round(y1, 2)],
            "size": [round(x1 - x0, 2), round(y1 - y0, 2)]}


def build(name: str) -> int:
    d = Design(name)
    L, out = d.L, OUT / name
    out.mkdir(parents=True, exist_ok=True)
    row_gp, col_gp = matrix_pins(name)
    fw_row = lambda k: len(L.rows) - 1 - k.row  # noqa: E731  firmware row 0 = back row; the CAD counts from the front
    report = {"edition": name, "clearance_mm": CLEAR, "boards": {}, "connectors": d.connectors}
    plate_layers = [svg_paths(d.plate_shadow, "#e6e1d8", "#b9b1a3", L.V)]
    for bname, (cs, keys) in d.boards.items():
        write_dxf(out / f"{bname}.dxf", cs)
        plate_layers.append(svg_paths(cs, "#3f7d5c55", "#1f5a3c", L.V))
        plate_layers += [f'<rect x="{k.u - 7:.2f}" y="{L.V - k.v - 7:.2f}" width="14" height="14" fill="none" '
                         f'stroke="#222" stroke-width="0.3"/><text x="{k.u - 4:.2f}" y="{L.V - k.v + 1:.2f}" '
                         f'font-size="3" font-family="monospace">R{fw_row(k)}C{k.col}</text>' for k in keys]
        report["boards"][bname] = {
            "frame": "plate frame: u along the case's width from its left edge, v up the slope from the front edge",
            "outline": bounds(cs), "area_mm2": round(cs.area(), 1),
            "polygons": [[[round(x, 4), round(y, 4)] for x, y in poly] for poly in polygons(cs)],
            "matrix": "COL2ROW: switch from the column net to a diode, cathode to the row net",
            "switches": [{"ref": f"SW_R{fw_row(k)}C{k.col}", "row": fw_row(k), "col": k.col,
                          "row_net": f"ROW{fw_row(k)}_GP{row_gp[fw_row(k)]}", "col_net": f"COL{k.col}_GP{col_gp[k.col]}",
                          "u": round(k.u, 3), "v": round(k.v, 3)} for k in keys],
        }
    for label, c in d.connectors.items():
        if c["board"] != "hat":
            plate_layers.append(f'<rect x="{c["u"][0]}" y="{L.V - c["v"][1]:.2f}" width="{c["u"][1] - c["u"][0]:.2f}" '
                                f'height="{c["v"][1] - c["v"][0]:.2f}" fill="#c2410c88" stroke="#7c2d12" '
                                f'stroke-width="0.2"/><text x="{c["u"][0]:.2f}" y="{L.V - c["v"][1] - 0.6:.2f}" '
                                f'font-size="2.2" font-family="sans-serif">{label}</text>')
    write_svg(out / "plate_preview.svg", f"{name}: switch boards under the top plate (plate frame, mm)", L.W, L.V,
              plate_layers)
    write_dxf(out / "hat.dxf", d.hat)
    hat_layers = [svg_paths(d.hat_shadow, "#e6e1d8", "#b9b1a3", L.D), svg_paths(d.hat, "#3f6f9d55", "#1f3f6a", L.D)]
    hat_layers += [f'<circle cx="{p["x"]}" cy="{L.D - p["y"]}" r="0.8" fill="#222"/>' for p in d.pins]
    for label, c in d.connectors.items():
        if c["board"] == "hat":
            hat_layers.append(f'<rect x="{c["x"][0]}" y="{L.D - c["y"][1]:.2f}" width="{c["x"][1] - c["x"][0]:.2f}" '
                              f'height="{c["y"][1] - c["y"][0]:.2f}" fill="#c2410c88" stroke="#7c2d12" '
                              f'stroke-width="0.2"/><text x="{c["x"][0]:.2f}" y="{L.D - c["y"][1] - 0.6:.2f}" '
                              f'font-size="2.2" font-family="sans-serif">{label}</text>')
    write_svg(out / "hat_preview.svg", f"{name}: hat on the Pico 2, {HAT_Z[0]}..{HAT_Z[1]} mm above the desk (world, mm)",
              L.W, L.D, hat_layers)
    report["boards"]["hat"] = {"frame": "world: x along the case's width, y from the front edge to the back",
                               "height_mm": {"bottom": HAT_Z[0], "parts_up_to": HAT_Z[1]}, "outline": bounds(d.hat),
                               "area_mm2": round(d.hat.area(), 1), "pico_pins": d.pins,
                               "pico_screw_holes": {"diameter_mm": SCREW_HOLE, "centres": d.screw_holes},
                               "polygons": [[[round(x, 4), round(y, 4)] for x, y in poly] for poly in polygons(d.hat)]}
    (out / "positions.json").write_text(json.dumps(report, indent=2) + "\n")

    results = d.check()
    bad = [r for r in results if not r["ok"]]
    (out / "fit.json").write_text(json.dumps({"pairs": len(results), "failing": len(bad), "results": results},
                                             indent=1) + "\n")
    new_ballast, old_v, new_v = d.ballast()
    write_stl(out / "ballast_pcb.stl", new_ballast)
    print(f"{name}: boards " + ", ".join(f"{b} {v['outline']['size'][0]:.1f} x {v['outline']['size'][1]:.1f} mm"
                                         for b, v in report["boards"].items()))
    print(f"{name}: fit check {len(results)} pairs, {len(bad)} failing")
    for r in sorted(bad, key=lambda r: -r["overlap_mm3"]):
        print(f"  FAIL {r['a']:>22} x {r['b']:<24} {r['overlap_mm3']:9.3f} mm3")
    print(f"{name}: ballast {old_v * DENSITY / 1000:.0f} g -> {new_v * DENSITY / 1000:.0f} g (solid PLA)")
    return 1 if bad else 0


def main() -> int:
    return max(build(n) for n in (sys.argv[1:] or EDITIONS))


if __name__ == "__main__":
    sys.exit(main())
