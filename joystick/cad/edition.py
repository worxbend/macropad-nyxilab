"""Joystick edition: a KY-023 thumbstick standing on posts in the base.

The stock knob's dome sweeps wide when the stick is tilted, so the module cannot
hang from the plate: it stands on four tilted posts that grow out of the base, and
the plate only has a bevelled opening for the dome.  Everything that is specific to
this edition lives here; the shared case is in common/cad/macropad_cad.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from functools import lru_cache

from build123d import Align, Axis, Cone, Cylinder, Location, Part, Plane, Polyline, Pos, make_face, revolve

from macropad_cad.components import CXY, box, cyl
from macropad_cad.edition import Edition
from macropad_cad.fit_tests import base_patch, plate_patch
from macropad_cad.layout import Layout

BIG = 400.0


@dataclass(frozen=True)
class JoystickKY023:
    """KY-023 / HW-504 thumb joystick.

    Frame: origin on the stick axis at the PCB top surface.  +x = away from the
    header (header on the -x short edge), +y = away from the tact switch
    (switch on the -y long edge), +z = up the stick.
    Numbers from ibitlab/pub-3d-print-pulnuk-openscad measurements + photos.
    """

    pcb_l: float = 34.4
    pcb_w: float = 26.07
    pcb_t: float = 1.6
    stick_from_switch_edge: float = 14.05
    hole_d: float = 3.2
    hole_dx: float = 26.7  # MEASURE (community values disagree: 26.5-26.7)
    hole_dy: float = 20.3  # MEASURE (19.0-20.3)
    frame_w: float = 16.0
    frame_top: float = 12.2  # steel frame top above the PCB top
    pivot_below_frame: float = 6.7
    mech_r: float = 13.6  # pots / switch stay inside this radius
    pot_top_below_frame: float = 1.0
    dome_rim_d: float = 26.8
    dome_gap: float = 0.5  # dome rim above the frame top at rest
    neck_d: float = 9.8
    neck_h: float = 6.0
    cap_d: float = 21.0
    cap_h: float = 4.6
    tilt_max_deg: float = 25.0
    press_travel: float = 1.5
    header_out: float = 8.5  # right-angle header reach beyond the PCB edge
    joints_below: float = 1.5

    @property
    def pcb_cy(self) -> float:
        """PCB centre y relative to the stick axis."""
        return self.pcb_w / 2 - self.stick_from_switch_edge

    @property
    def pivot_z(self) -> float:
        return self.frame_top - self.pivot_below_frame

    @property
    def dome_r(self) -> float:
        """Dome outer surface: a sphere around the pivot through the rim."""
        h = self.pivot_below_frame + self.dome_gap
        return math.hypot(self.dome_rim_d / 2, h)


@dataclass(frozen=True)
class JoystickMount:
    """How the module sits in the case."""

    g: float = 1.5  # plate top surface above the steel frame
    base_relief: float = 1.2  # pocket in the base under the solder joints
    hole_d: float = 32.0  # plate opening (clears the dome sweep)
    hole_chamfer: float = 1.5  # 45 deg bevel on top
    post_d: float = 7.0  # base posts, perpendicular to the plate, M3 self-tapping


@dataclass(frozen=True)
class JoystickParams:
    joy: JoystickKY023 = field(default_factory=JoystickKY023)
    mount: JoystickMount = field(default_factory=JoystickMount)


class JoystickEdition(Edition):
    name = "joystick"
    title = "JOYSTICK EDITION"
    centre_label = "Joystick"
    component_colors = {"knob": "#1C1C1E"}
    component_overrides = {("joystick", "pcb"): "#151515"}
    base_mounted_components = ("joystick",)
    base_subtitle = "Pico 2 cradle, LED & joystick posts, M3 countersinks, feet"
    bom_hardware = [["M3x6 self-tapping", "4", "joystick posts"]]
    fit_lines_after_bar = ["fit_plate_joy   joystick opening and bevel"]
    fit_lines_after_usb = ["fit_joystick    base patch with the 4 posts: check the KY-023 hole grid"]

    def __init__(self, p: JoystickParams = JoystickParams()):
        self.p = p

    # ------------------------------------------------------------------ frames
    def loc(self, L: Layout) -> Location:
        """KY-023 frame (stick axis, PCB top) -> world."""
        j, m = self.p.joy, self.p.mount
        return L.ploc * Pos(L.u_c, L.v_c, -(m.g + j.frame_top))

    def holes_local(self) -> list[tuple[float, float]]:
        j = self.p.joy
        return [(sx * j.hole_dx / 2, j.pcb_cy + sy * j.hole_dy / 2) for sy in (-1, 1) for sx in (-1, 1)]

    # -------------------------------------------------------- reference model
    def knob_profile_face(self):
        """Half cross-section (x = radius, z = height) of the stock PS2 knob, at rest."""
        j = self.p.joy
        zp = j.pivot_z
        R = j.dome_r
        z_rim = j.frame_top + j.dome_gap
        z_dome_top = zp + R
        pts = [(0, z_rim)]
        # dome arc sampled from the rim up to where the neck leaves it
        phi0 = math.atan2(j.dome_rim_d / 2, z_rim - zp)
        phi_neck = math.asin(j.neck_d / 2 / R)
        n = 24
        for i in range(n + 1):
            ph = phi0 + (phi_neck - phi0) * i / n
            pts.append((R * math.sin(ph), zp + R * math.cos(ph)))
        z_neck_top = z_dome_top + j.neck_h
        pts += [(j.neck_d / 2, z_neck_top), (j.cap_d / 2 - 0.8, z_neck_top), (j.cap_d / 2, z_neck_top + 0.8),
                (j.cap_d / 2, z_neck_top + j.cap_h - 0.6), (j.cap_d / 2 - 1.0, z_neck_top + j.cap_h),
                (0, z_neck_top + j.cap_h - 0.8)]
        return make_face(Polyline(*pts, close=True))

    @lru_cache(maxsize=None)
    def knob_model(self) -> Part:
        return revolve(Plane.XZ * self.knob_profile_face(), Axis.Z, 360)

    @lru_cache(maxsize=None)
    def knob_sweep(self) -> Part:
        """Everything the knob dome can reach: full tilt in any direction + press travel."""
        j = self.p.joy
        zp = j.pivot_z
        R = j.dome_r + 0.4
        z_rim = j.frame_top + j.dome_gap
        phi_max = math.atan2(j.dome_rim_d / 2, z_rim - zp) + math.radians(j.tilt_max_deg)
        n = 40
        pts = [(0.0, zp)]
        for i in range(n + 1):
            ph = phi_max * (1 - i / n)
            pts.append((R * math.sin(ph), zp + R * math.cos(ph)))
        face = Plane.XZ * make_face(Polyline(*pts, close=True))
        sweep = revolve(face, Axis.Z, 360)
        return sweep + Pos(0, 0, -j.press_travel) * sweep

    @lru_cache(maxsize=None)
    def model(self) -> dict[str, Part]:
        j = self.p.joy
        pcb = box(-j.pcb_l / 2, j.pcb_l / 2, j.pcb_cy - j.pcb_w / 2, j.pcb_cy + j.pcb_w / 2, -j.pcb_t, 0)
        for sx in (-1, 1):
            for sy in (-1, 1):
                pcb -= cyl(sx * j.hole_dx / 2, j.pcb_cy + sy * j.hole_dy / 2, -3, 1, j.hole_d)
        frame = box(-j.frame_w / 2, j.frame_w / 2, -j.frame_w / 2, j.frame_w / 2, 0.3, j.frame_top)
        frame -= cyl(0, 0, j.frame_top - 1.5, j.frame_top + 1, 11.0)
        pot_top = j.frame_top - j.pot_top_below_frame
        pots = box(8.0, 12.5, -5.0, 5.0, 0.5, pot_top) + box(-5.0, 5.0, 8.0, 12.5, 0.5, pot_top)
        tact = box(-3.0, 3.0, -12.8, -7.5, 0.0, 4.0)
        hdr_y0 = j.pcb_cy - 6.35
        header = box(-j.pcb_l / 2, -j.pcb_l / 2 + 2.5, hdr_y0, hdr_y0 + 12.7, 0, 2.5)
        for k in range(5):
            y = j.pcb_cy + (k - 2) * 2.54
            header += box(-j.pcb_l / 2 - 6.0, -j.pcb_l / 2 + 1.5, y - 0.32, y + 0.32, 0.95, 1.6)
        return {"pcb": pcb, "mech": frame + pots + tact, "header": header, "knob": self.knob_model()}

    @lru_cache(maxsize=None)
    def keepout(self) -> Part:
        """Module without the knob (the knob is checked with ``knob_sweep``).

        The solder-joint layer under the PCB excludes the four mounting holes,
        where the posts support the board.
        """
        j = self.p.joy
        ko = box(-j.pcb_l / 2, j.pcb_l / 2, j.pcb_cy - j.pcb_w / 2, j.pcb_cy + j.pcb_w / 2, -j.pcb_t, 0)
        joints = box(-j.pcb_l / 2, j.pcb_l / 2, j.pcb_cy - j.pcb_w / 2, j.pcb_cy + j.pcb_w / 2,
                     -j.pcb_t - j.joints_below, -j.pcb_t)
        for sx in (-1, 1):
            for sy in (-1, 1):
                joints -= cyl(sx * j.hole_dx / 2, j.pcb_cy + sy * j.hole_dy / 2, -10, 1, 7.6)
        ko += joints
        ko += Pos(0, 0, 0.3) * Cylinder(j.mech_r, j.frame_top - j.pot_top_below_frame - 0.3, align=CXY)
        ko += box(-j.frame_w / 2, j.frame_w / 2, -j.frame_w / 2, j.frame_w / 2, 0.3, j.frame_top)
        hdr_y0 = j.pcb_cy - 6.6
        ko += box(-j.pcb_l / 2 - j.header_out, -j.pcb_l / 2 + 2.5, hdr_y0, hdr_y0 + 13.2, -j.pcb_t - j.joints_below, 3.0)
        return ko

    # ---------------------------------------------------------- case geometry
    def plate_cuts(self, L: Layout) -> Part:
        """Opening for the dome sweep, bevelled on top."""
        m, T = self.p.mount, L.p.case.plate_t
        jc = m.hole_chamfer
        cuts = Pos(0, 0, -T - 5) * Cylinder(m.hole_d / 2, T + 6, align=CXY)
        cuts += Pos(0, 0, 0.3) * Cone(m.hole_d / 2, m.hole_d / 2 + jc + 0.3, jc + 0.3,
                                      align=(Align.CENTER, Align.CENTER, Align.MAX))
        return L.loc_centre * cuts

    def base_features(self, L: Layout, body: Part) -> Part:
        """Relief for the solder joints, then posts perpendicular to the plate (M3 self-tapping)."""
        j, m = self.p.joy, self.p.mount
        c, fas = L.p.case, L.p.fas
        jl = self.loc(L)
        if m.base_relief > 0:
            foot = jl * box(-j.pcb_l / 2 - 1, j.pcb_l / 2 + 1, j.pcb_cy - j.pcb_w / 2 - 1, j.pcb_cy + j.pcb_w / 2 + 1,
                            -60, 0)
            body -= foot & box(-BIG, BIG, -BIG, BIG, c.base_t - m.base_relief, c.base_t + 0.01)
        for hx, hy in self.holes_local():
            post = jl * cyl(hx, hy, -60, -j.pcb_t, m.post_d)
            body += post & box(-BIG, BIG, -BIG, BIG, 0.6, BIG)
        for hx, hy in self.holes_local():
            top = (jl * Pos(hx, hy, -j.pcb_t)).position
            depth = (top.Z - 0.8) / L.cos
            body -= jl * cyl(hx, hy, -j.pcb_t - depth, -j.pcb_t + 1, fas.m3_pilot_d)
        return body

    def components(self, L: Layout) -> dict[str, dict[str, Part]]:
        loc = self.loc(L)
        return {"joystick": {n: loc * s for n, s in self.model().items()}}

    def keepouts(self, L: Layout) -> dict[str, Part]:
        return {"joystick": self.loc(L) * self.keepout()}

    def sweep(self, L: Layout) -> tuple[str, Part]:
        return "joystick knob sweep", self.loc(L) * self.knob_sweep()

    # --------------------------------------------------------------- checks
    def fit_tests(self, L: Layout) -> dict:
        return {"fit_plate_joy": self.fit_plate_joy, "fit_joystick": self.fit_joystick}

    def fit_plate_joy(self, L: Layout) -> Part:
        r = self.p.mount.hole_d / 2 + 5.5  # stays clear of the neighbouring case bosses
        region = L.ploc * box(L.u_c - r, L.u_c + r, L.v_c - r, L.v_c + r, -L.p.case.plate_t - 4, 2)
        return plate_patch(L, region)

    def fit_joystick(self, L: Layout) -> Part:
        """Base patch with the four posts: lay the module on, check the hole grid."""
        j = self.p.joy
        pts = [(self.loc(L) * Pos(hx, hy, -j.pcb_t)).position for hx, hy in self.holes_local()]
        x0, x1 = min(p.X for p in pts) - 7, max(p.X for p in pts) + 7
        y0, y1 = min(p.Y for p in pts) - 7, max(p.Y for p in pts) + 8
        return base_patch(L, box(x0, x1, y0, y1, -1, 40))

    def clearances(self, L: Layout) -> dict[str, float]:
        j, m, c = self.p.joy, self.p.mount, L.p.case
        # PCB underside (incl. joints) above the base, at its lowest (front) edge
        front = (self.loc(L) * Pos(0, j.pcb_cy - j.pcb_w / 2, -j.pcb_t - j.joints_below)).position
        h_under = m.g + j.pivot_below_frame - c.plate_t
        r_sweep = math.sqrt(max((j.dome_r + 0.4) ** 2 - h_under**2, 0))
        return {
            "joystick joints above base": front.Z - (c.base_t - m.base_relief),
            "joystick hole radial clearance": m.hole_d / 2 - r_sweep,
        }

    def summary(self, L: Layout) -> dict:
        return {"joystick v": L.v_c}

    def viewer_info(self, L: Layout, clearances: dict[str, float]) -> dict:
        return {
            "centre": f"KY-023, {self.p.joy.tilt_max_deg:.0f}° swing, "
                      f"{clearances['joystick hole radial clearance']:.2f} mm clear",
            "section_note": f"{L.u_c:.1f} mm passes through the joystick, main display, Pico and USB-C port",
        }

    # -------------------------------------------------------------- drawings
    def section_styles(self, comp: dict) -> list:
        j = comp["joystick"]
        return [(j["pcb"], "#3a3a3a", "#d9d9d9", "xxxx"), (j["mech"], "#a3a3a3", "#e6e6e6", ""),
                (j["knob"], "#2b2b2b", "#cfcfcf", "")]

    def section_callouts(self, L: Layout) -> list:
        return [((L.v_c * L.cos + 16, L.H_f + 21), "joystick knob")]

    def section_png_labels(self, L: Layout) -> list:
        return [(L.v_c * L.cos + 17, L.H_f + 21, "joystick")]

    def plate_notes(self, L: Layout) -> list[str]:
        m = self.p.mount
        return [f"Joystick opening D{m.hole_d:.1f} + {m.hole_chamfer:.1f} x 45 bevel at (u {L.u_c:.2f}, v {L.v_c:.2f})"]

    def base_notes(self, L: Layout) -> list[str]:
        j, m, c, fas = self.p.joy, self.p.mount, L.p.case, L.p.fas
        jh = [(self.loc(L) * Pos(hx, hy, -j.pcb_t)).position for hx, hy in self.holes_local()]
        return [
            f"Joystick posts D{m.post_d:.1f}, perpendicular to the plate ({c.tilt_deg:.0f} deg), "
            f"M3 pilot D{fas.m3_pilot_d:.1f}",
            "   top centres: " + ", ".join(f"({q.X:.1f}, {q.Y:.1f}, z{q.Z:.1f})" for q in jh[:2]),
            "                " + ", ".join(f"({q.X:.1f}, {q.Y:.1f}, z{q.Z:.1f})" for q in jh[2:]),
            f"Joystick solder-joint relief {m.base_relief:.1f} deep under the module",
        ]


EDITION = JoystickEdition()
