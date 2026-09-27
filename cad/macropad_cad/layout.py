"""Derived positions: key grid, module placements, bosses, heights.

Nothing here builds geometry; it only turns ``params`` into coordinates so
that parts, components, checks and drawings all agree on the same numbers.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from functools import cached_property

from build123d import Location, Plane, Pos, Rot, Vector

from .params import P, Params


@dataclass(frozen=True)
class Key:
    col: int
    row: int
    u: float
    v: float

    @property
    def name(self) -> str:
        return f"K{self.row}{self.col}"


class Layout:
    def __init__(self, p: Params = P):
        self.p = p
        c, sw, bar, tol = p.case, p.sw, p.bar, p.tol
        self.th = math.radians(c.tilt_deg)
        self.sin, self.cos, self.tan = math.sin(self.th), math.cos(self.th), math.tan(self.th)

        # ---- world footprint -------------------------------------------------
        self.D = c.depth
        self.V = self.D / self.cos  # slope length of the top surface
        self.H_f = c.front_h
        self.H_b = self.H_f + self.D * self.tan

        # ---- columns (u) -------------------------------------------------------
        self.u_bar0 = c.bar_pcb_u0
        self.u_bar = self.u_bar0 + bar.pcb_w / 2
        self.u_L = self.u_bar0 + bar.pcb_w + tol.pocket + 1.2 + sw.pocket_u / 2 + c.key_gap_after_bar
        self.u_c = self.u_L + c.center_half
        self.u_R1 = self.u_c + c.center_half
        self.u_R2 = self.u_R1 + sw.pitch
        self.W = round(self.u_R2 + sw.cap / 2 + c.right_margin, 2)

        # ---- rows (v) ----------------------------------------------------------
        self.rows = [c.rows_v0 + (i + 0.5) * sw.pitch for i in range(4)]
        self.v_j = c.joy_v
        self.v_w = (self.rows[2] + self.rows[3]) / 2
        self.v_bar = c.bar_v

        cols = [self.u_L, self.u_R1, self.u_R2]
        self.keys = [Key(ci, ri, cols[ci], self.rows[ri]) for ri in range(4) for ci in range(3)]

    # ------------------------------------------------------------------ frames
    @cached_property
    def plane(self) -> Plane:
        """Plate frame: origin at the front-left top corner, z = outward normal."""
        return Plane(origin=(0, 0, self.H_f), x_dir=(1, 0, 0), z_dir=(0, -self.sin, self.cos))

    @cached_property
    def ploc(self) -> Location:
        return Location(self.plane)

    def world(self, u: float, v: float, w: float = 0.0) -> Vector:
        return Vector(u, v * self.cos - w * self.sin, self.H_f + v * self.sin + w * self.cos)

    def z_top(self, y: float) -> float:
        return self.H_f + y * self.tan

    def z_under(self, y: float) -> float:
        return self.z_top(y) - self.p.case.plate_t / self.cos

    # ------------------------------------------------------------- placements
    @cached_property
    def loc_d19(self) -> Location:
        """1.9" module frame -> world.  Header end on the right (+u)."""
        d = self.p.d19
        aa_cx = d.aa_x0 + d.aa_l / 2
        return self.ploc * Pos(self.u_c + aa_cx, self.v_w, -self.p.case.disp_skin) * Rot(0, 0, 180)

    @cached_property
    def loc_bar(self) -> Location:
        """2.25" bar frame -> world.  Holes at the back (+v), header at the front."""
        b = self.p.bar
        return self.ploc * Pos(self.u_bar, self.v_bar + b.aa_cx, -self.p.case.disp_skin) * Rot(0, 0, -90)

    @cached_property
    def loc_joy(self) -> Location:
        """KY-023 frame (stick axis, PCB top) -> world."""
        j, c = self.p.joy, self.p.case
        return self.ploc * Pos(self.u_c, self.v_j, -(c.joy_g + j.frame_top))

    @cached_property
    def usb_face_y(self) -> float:
        c = self.p.case
        return self.D - (c.wall - c.usb_thin_wall)

    @cached_property
    def pico_edge_y(self) -> float:
        return self.usb_face_y - self.p.pico.usb_overhang

    @cached_property
    def pico_z(self) -> float:
        """Pico PCB bottom."""
        return self.p.case.base_t + self.p.pico.standoff_h

    @cached_property
    def pico_x(self) -> float:
        return self.W / 2

    @cached_property
    def loc_pico(self) -> Location:
        """Pico frame (x from the USB edge towards the debug end, z from PCB bottom) -> world."""
        return Location((self.pico_x, self.pico_edge_y, self.pico_z)) * Rot(0, 0, -90)

    @cached_property
    def usb_center(self) -> Vector:
        pc = self.p.pico
        return Vector(self.pico_x, self.usb_face_y, self.pico_z + pc.t + pc.usb_h / 2)

    # ------------------------------------------------------------------ bosses
    @cached_property
    def bosses(self) -> list[tuple[str, float, float]]:
        i = self.p.case.boss_inset
        W, D = self.W, self.D
        return [
            ("FL", i, i),
            ("FR", W - i, i),
            ("BL", i, D - i),
            ("BR", W - i, D - i),
            ("F1", self.u_c - 26.0, i),
            ("F2", self.u_c + 26.0, i),
            ("B1", self.pico_x - 22.0, D - i),
            ("B2", self.pico_x + 22.0, D - i),
        ]

    # -------------------------------------------------------- joystick holes
    @cached_property
    def joy_holes_local(self) -> list[tuple[float, float]]:
        j = self.p.joy
        return [(sx * j.hole_dx / 2, j.pcb_cy + sy * j.hole_dy / 2) for sy in (-1, 1) for sx in (-1, 1)]

    def summary(self) -> dict:
        return {
            "W": self.W,
            "D": self.D,
            "V (slope length)": round(self.V, 2),
            "tilt_deg": self.p.case.tilt_deg,
            "front height": self.H_f,
            "back height": round(self.H_b, 2),
            "u_bar": round(self.u_bar, 2),
            "u_L": round(self.u_L, 2),
            "u_c": round(self.u_c, 2),
            "u_R1": round(self.u_R1, 2),
            "u_R2": round(self.u_R2, 2),
            "rows v": [round(r, 2) for r in self.rows],
            "joystick v": self.v_j,
            "main display v": round(self.v_w, 2),
            "bar display v": self.v_bar,
            "usb center": tuple(round(x, 2) for x in self.usb_center),
        }


L = Layout()
