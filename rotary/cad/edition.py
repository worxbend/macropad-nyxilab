"""Rotary edition: a KY-040 encoder panel-mounted in the top plate, with a printed knob.

The encoder's M7 bushing goes up through the plate; its nut clamps a 2 mm section
from the top and a square pocket from below keys the body so it cannot turn.  The
printed knob is a press fit on the D-shaft, its bore ending at the shaft tip so the
knob stops at the right height and a push goes straight into the encoder's switch.
Everything specific to this edition lives here; the shared case is in
common/cad/macropad_cad.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from functools import lru_cache

from build123d import Cylinder, Location, Part, Pos, RegularPolygon, Rot, Vector, chamfer, extrude

from macropad_cad.components import CXY, box, cyl
from macropad_cad.edition import Edition
from macropad_cad.fit_tests import plate_patch
from macropad_cad.layout import Layout
from macropad_cad.parts import outer_edges_of_face


@dataclass(frozen=True)
class EncoderKY040:
    """KY-040 rotary encoder module (EC11-type encoder with push switch).

    Frame: origin on the shaft axis at the encoder body's TOP face (the face
    that bears against the plate), +z up the shaft.  +x = away from the 5-pin
    header (header on the -x short edge), +y = away from the mounting-hole edge.
    Encoder data: Alps EC11 drawing; module data: listings + photos (MEASURE).
    """

    pcb_l: float = 24.0  # MEASURE (listings: 32 incl. the header pins)
    pcb_w: float = 18.0  # MEASURE (listings: 19)
    pcb_t: float = 1.6
    axis_dx: float = 1.4  # MEASURE: shaft axis from the PCB centre, away from the header
    axis_dy: float = 1.15  # MEASURE: ... away from the mounting-hole edge
    hole_d: float = 2.75
    hole_pitch: float = 13.75  # MEASURE (listings 13.75, photo ~12.6); holes are not used for mounting
    body_w: float = 11.7
    body_l: float = 12.0
    body_h: float = 6.5  # MEASURE: PCB top to the body's top face
    bushing_d: float = 7.0  # M7 x 0.75 thread
    bushing_h: float = 6.0  # MEASURE (Alps EC11: 7.0)
    shaft_d: float = 6.0
    shaft_flat: float = 4.5  # D-shaft: width across the flat
    shaft_len: float = 20.0  # MEASURE: from the body's top face to the shaft tip (15 / 20 / 25 mm versions exist)
    flat_len: float = 10.0
    nut_af: float = 9.0  # hex nut across flats
    nut_t: float = 2.0
    washer_d: float = 10.0
    washer_t: float = 0.5
    header_out: float = 8.5
    joints_below: float = 1.5
    detents: int = 20
    push_travel: float = 0.5  # EC11 push switch travel


@dataclass(frozen=True)
class Knob:
    """Printed knob (press-fit on the D-shaft)."""

    d: float = 30.0
    h: float = 19.0
    top_chamfer: float = 1.2
    bottom_chamfer: float = 0.6
    skirt_d: float = 17.0  # recess underneath for the nut, washer and bushing
    skirt_h: float = 5.5
    bore_clear: float = 0.10  # added to the D-bore; 0 = tighter, 0.2 = looser
    bore_extra: float = 0.0  # 0 = the bore ends at the shaft tip: the knob presses on until it stops,
    #                           and pushing the knob pushes the shaft (the switch), not the knob down the shaft
    flutes: int = 36
    flute_r: float = 0.9
    dot_d: float = 2.6  # indicator dimple on the top face
    dot_depth: float = 0.6
    dot_r: float = 10.0
    gap: float = 1.0  # knob skirt above the plate surface


@dataclass(frozen=True)
class EncoderMount:
    """How the module sits in the plate."""

    ring_d: float = 36.0  # engraved ring around the knob (decorative, prints as tiny bridges)
    ring_w: float = 0.8
    ring_depth: float = 0.5
    clamp_t: float = 2.0  # plate thickness clamped by the encoder nut (top surface down to the body)
    hole_d: float = 7.4  # M7 bushing
    pocket: float = 12.5  # square pocket from below: keys the encoder body so it cannot spin


@dataclass(frozen=True)
class RotaryParams:
    enc: EncoderKY040 = field(default_factory=EncoderKY040)
    knob: Knob = field(default_factory=Knob)
    mount: EncoderMount = field(default_factory=EncoderMount)


class RotaryEdition(Edition):
    name = "rotary"
    title = "ROTARY EDITION"
    centre_label = "Encoder"
    part_colors = {"knob": "#5B3E8E"}  # same accent as the frame
    component_colors = {"enc_base": "#2A9D8F", "shaft": "#C8CCD0"}
    component_overrides = {("encoder", "pcb"): "#151515"}
    print_notes = {"knob": "Print as exported (top face down). 0.12-0.16 mm layers, 4 walls, 30% infill. "
                           "Press onto the D-shaft."}
    render_lift = {"knob": 2.0}  # moves with the plate in exploded views
    plate_mounted_components = ("encoder",)
    underside_extra_parts = ("knob",)
    sweep_exclude = ("knob",)  # the knob is the thing that spins
    tilt_label_frac = 0.72
    bom_printed = [["knob (colour B)", "1", "PLA/PETG, top face down, press fit"]]
    bom_hardware = [["M7 nut + washer (KY-040 kit)", "1", "encoder panel mount"]]
    fit_lines_after_bar = ["fit_plate_enc   encoder mount: M7 bushing hole, square key pocket, nut clamp"]
    fit_lines_after_usb = ["knob            print it early too: it should press onto the D-shaft firmly"]
    sheet5_title = "SPINE, KNOB & FIT TESTS"
    sheet5_subtitle = "Bar-display clamp, the knob, coupons to print first"
    sheet5_name = "05_spine_knob_and_fit_tests"

    def __init__(self, p: RotaryParams = RotaryParams()):
        self.p = p

    # ------------------------------------------------------------------ frames
    @property
    def w_face(self) -> float:
        """Plate-frame depth of the encoder body's top face (it bears on the clamp section)."""
        return -self.p.mount.clamp_t

    def loc(self, L: Layout) -> Location:
        """KY-040 frame (shaft axis, body top face) -> world.  Header to the left."""
        return L.ploc * Pos(L.u_c, L.v_c, self.w_face)

    def loc_knob(self, L: Layout) -> Location:
        """Knob frame (axis, bottom face) -> world."""
        return L.ploc * Pos(L.u_c, L.v_c, self.p.knob.gap)

    def shaft_tip(self) -> float:
        """Shaft tip above the knob's bottom face."""
        e, k, m = self.p.enc, self.p.knob, self.p.mount
        return e.shaft_len - m.clamp_t - k.gap

    def top_z(self, L: Layout) -> float:
        """Height of the knob's highest point above the desk."""
        k = self.p.knob
        return (self.loc_knob(L) * Pos(0, k.d / 2 - k.top_chamfer, k.h)).position.Z

    # -------------------------------------------------------- reference model
    @lru_cache(maxsize=None)
    def model(self) -> dict[str, Part]:
        """KY-040 in its frame: shaft axis, z = 0 on the encoder body's top face."""
        e, m = self.p.enc, self.p.mount
        cx, cy = -e.axis_dx, -e.axis_dy  # PCB centre
        z_top = -e.body_h
        pcb = box(cx - e.pcb_l / 2, cx + e.pcb_l / 2, cy - e.pcb_w / 2, cy + e.pcb_w / 2, z_top - e.pcb_t, z_top)
        for sx in (-1, 1):
            pcb -= cyl(cx + sx * e.hole_pitch / 2, cy - e.pcb_w / 2 + 2.0, z_top - 3, z_top + 1, e.hole_d)
        base = box(-e.body_w / 2, e.body_w / 2, -e.body_l / 2, e.body_l / 2, -e.body_h, -e.body_h + 2.2)
        frame = box(-e.body_w / 2, e.body_w / 2, -e.body_l / 2, e.body_l / 2, -e.body_h + 2.2, 0)
        bushing = cyl(0, 0, 0, e.bushing_h, e.bushing_d)
        shaft = cyl(0, 0, 0, e.shaft_len, e.shaft_d)
        flat_x = e.shaft_flat - e.shaft_d / 2
        shaft -= box(flat_x, e.shaft_d, -e.shaft_d, e.shaft_d, e.shaft_len - e.flat_len, e.shaft_len + 1)
        z_w = m.clamp_t
        washer = cyl(0, 0, z_w, z_w + e.washer_t, e.washer_d) - cyl(0, 0, z_w - 1, z_w + 1, e.bushing_d + 0.2)
        hexagon = RegularPolygon(e.nut_af / 2 / math.cos(math.pi / 6), 6)
        nut = Pos(0, 0, z_w + e.washer_t) * extrude(hexagon, amount=e.nut_t)
        nut -= cyl(0, 0, z_w, z_w + e.washer_t + e.nut_t + 1, e.bushing_d)
        hdr_x = cx - e.pcb_l / 2
        header = box(hdr_x, hdr_x + 2.5, cy - 6.35, cy + 6.35, z_top, z_top + 2.5)
        for k in range(5):
            y = cy + (k - 2) * 2.54
            header += box(hdr_x - 6.0, hdr_x + 1.5, y - 0.32, y + 0.32, z_top + 0.95, z_top + 1.6)
        return {"pcb": pcb, "enc_base": base, "mech": frame + bushing + washer + nut, "shaft": shaft, "header": header}

    @lru_cache(maxsize=None)
    def keepout(self) -> Part:
        """Module + solder joints + header; the parts that pass through the plate are exact."""
        e, m = self.p.enc, self.p.mount
        cx, cy = -e.axis_dx, -e.axis_dy
        z_top = -e.body_h
        ko = box(cx - e.pcb_l / 2 - 1.0, cx + e.pcb_l / 2 + 1.0, cy - e.pcb_w / 2 - 1.0, cy + e.pcb_w / 2 + 1.0,
                 z_top - e.pcb_t - e.joints_below, z_top)
        ko += box(-e.body_w / 2, e.body_w / 2, -e.body_l / 2, e.body_l / 2, -e.body_h, 0)
        ko += cyl(0, 0, 0, e.bushing_h, e.bushing_d)
        shaft = cyl(0, 0, 0, e.shaft_len, e.shaft_d - 0.02)
        shaft -= box(e.shaft_flat - e.shaft_d / 2 - 0.01, e.shaft_d, -e.shaft_d, e.shaft_d, e.shaft_len - e.flat_len,
                     e.shaft_len + 1)
        ko += shaft
        z_w = m.clamp_t + 0.02
        ko += cyl(0, 0, z_w, z_w + e.washer_t + e.nut_t, e.nut_af / math.cos(math.pi / 6))
        hdr_x = cx - e.pcb_l / 2
        ko += box(hdr_x - e.header_out, hdr_x + 2.5, cy - 6.6, cy + 6.6, z_top - e.pcb_t - e.joints_below, z_top + 3.0)
        return ko

    def knob_envelope(self, margin: float = 0.4) -> Part:
        """Space the spinning knob needs (its cylinder + margin), in the knob frame."""
        k = self.p.knob
        return cyl(0, 0, -margin, k.h + margin, k.d + 2 * margin)

    # ------------------------------------------------------------ printed knob
    @lru_cache(maxsize=None)
    def knob_local(self) -> Part:
        """The knob in its own frame: axis on z, bottom face at z = 0.

        Fluted grip band, recess underneath for the nut/washer/bushing, a D-bore
        press-fit on the EC11 shaft and an indicator dimple on the top face.
        """
        k, e = self.p.knob, self.p.enc
        body = Cylinder(k.d / 2, k.h, align=CXY)
        body = chamfer(outer_edges_of_face(body, Vector(0, 0, 1)), k.top_chamfer)
        body = chamfer(outer_edges_of_face(body, Vector(0, 0, -1)), k.bottom_chamfer)
        z0, z1 = k.bottom_chamfer + 1.5, k.h - k.top_chamfer - 1.5
        for i in range(k.flutes):
            a = 2 * math.pi * i / k.flutes
            r = k.d / 2 + k.flute_r * 0.25
            body -= Pos(r * math.cos(a), r * math.sin(a), z0) * Cylinder(k.flute_r, z1 - z0, align=CXY)
        body -= Pos(0, 0, -1) * Cylinder(k.skirt_d / 2, k.skirt_h + 1, align=CXY)
        # bore: round where the shaft is round, D-shaped where it is flatted, ends at the tip
        tip = self.shaft_tip()
        flat_z = tip - e.flat_len + 0.5
        rb = (e.shaft_d + k.bore_clear) / 2
        flat = e.shaft_flat - e.shaft_d / 2 + k.bore_clear / 2
        bore = Pos(0, 0, k.skirt_h - 0.5) * Cylinder(rb, tip + k.bore_extra - k.skirt_h + 0.5, align=CXY)
        bore -= box(flat, rb + 1, -rb - 1, rb + 1, flat_z, k.h + 1)
        body -= bore
        body -= Pos(k.dot_r, 0, k.h - k.dot_depth) * Cylinder(k.dot_d / 2, k.dot_depth + 1, align=CXY)
        return body

    # ---------------------------------------------------------- case geometry
    def plate_cuts(self, L: Layout) -> Part:
        """Panel mount in the plate frame centred on the knob axis (w = 0 = top surface).

        A 2 mm clamp section for the M7 nut, a square pocket from below that keys
        the encoder body so it cannot turn, and a thin engraved ring around the knob.
        (A recessed well would print as a 34 mm bridge on the visible face.)
        """
        m, T = self.p.mount, L.p.case.plate_t
        ro, ri = m.ring_d / 2 + m.ring_w / 2, m.ring_d / 2 - m.ring_w / 2
        cuts = Pos(0, 0, -m.ring_depth) * (Cylinder(ro, m.ring_depth + 1, align=CXY)
                                           - Cylinder(ri, m.ring_depth + 2, align=CXY))
        cuts += Pos(0, 0, -T - 5) * Cylinder(m.hole_d / 2, T + 6, align=CXY)
        q = m.pocket / 2
        cuts += box(-q, q, -q, q, -T - 5, self.w_face)
        return L.loc_centre * cuts

    def parts(self, L: Layout) -> dict[str, Part]:
        return {"knob": self.loc_knob(L) * self.knob_local()}

    def print_pose(self, name: str, part: Part, L: Layout) -> Part | None:
        if name == "knob":
            return Rot(180, 0, 0) * self.knob_local()  # top face on the bed
        return None

    def components(self, L: Layout) -> dict[str, dict[str, Part]]:
        loc = self.loc(L)
        return {"encoder": {n: loc * s for n, s in self.model().items()}}

    def keepouts(self, L: Layout) -> dict[str, Part]:
        return {"encoder": self.loc(L) * self.keepout()}

    def sweep(self, L: Layout) -> tuple[str, Part]:
        return "knob envelope", self.loc_knob(L) * self.knob_envelope()

    # --------------------------------------------------------------- checks
    def fit_tests(self, L: Layout) -> dict:
        return {"fit_plate_enc": self.fit_plate_enc}

    def fit_plate_enc(self, L: Layout) -> Part:
        r = self.p.mount.ring_d / 2 + 3.5  # stays clear of the neighbouring case bosses
        region = L.ploc * box(L.u_c - r, L.u_c + r, L.v_c - r, L.v_c + r, -L.p.case.plate_t - 4, 2)
        return plate_patch(L, region)

    def clearances(self, L: Layout) -> dict[str, float]:
        e, k, m, c = self.p.enc, self.p.knob, self.p.mount, L.p.case
        # PCB underside (incl. joints) above the base, at its lowest (front) edge
        front = (self.loc(L) * Pos(0, -e.axis_dy - e.pcb_w / 2, -e.body_h - e.pcb_t - e.joints_below)).position
        tip = self.shaft_tip()
        return {
            "encoder joints above base": front.Z - c.base_t,
            "knob above the plate": k.gap,
            "engraved ring outside the knob": (m.ring_d - m.ring_w) / 2 - k.d / 2,
            "shaft engagement in the knob": tip - (k.skirt_h - 0.5),
            "D-flat engagement in the knob": tip - (tip - e.flat_len + 0.5),
            "knob top skin over the bore": k.h - (tip + k.bore_extra),
            "knob gap while pressed": k.gap - e.push_travel,
            "bushing thread above the nut": e.bushing_h - m.clamp_t - e.washer_t - e.nut_t,
        }

    def summary(self, L: Layout) -> dict:
        return {"rotary knob v": L.v_c, "knob top z": round(self.top_z(L), 2)}

    def viewer_info(self, L: Layout, clearances: dict[str, float]) -> dict:
        return {
            "centre": f"KY-040 + Ø{self.p.knob.d:.0f} knob, {self.p.enc.detents} detents",
            "height_extra": f" · knob top {self.top_z(L):.1f}",
            "section_note": f"{L.u_c:.1f} mm passes through the knob, encoder, main display, Pico and USB-C port",
        }

    # -------------------------------------------------------------- drawings
    def section_styles(self, comp: dict) -> list:
        e = comp["encoder"]
        return [(e["pcb"], "#3a3a3a", "#d9d9d9", "xxxx"), (e["enc_base"], "#2a9d8f", "#e6e6e6", ""),
                (e["mech"], "#a3a3a3", "#e6e6e6", ""), (e["shaft"], "#c8ccd0", "#cfcfcf", "")]

    def section_callouts(self, L: Layout) -> list:
        return [((L.v_c * L.cos + 26, L.H_f + 21.5), "printed knob"), ((L.v_c * L.cos + 1, 6.5), "KY-040")]

    def section_png_labels(self, L: Layout) -> list:
        return [(L.v_c * L.cos + 27, L.H_f + 24, "printed knob"), (L.v_c * L.cos, 8.5, "KY-040 encoder")]

    def side_view_dims(self, s, side, L: Layout) -> None:
        k = self.p.knob
        q = (self.loc_knob(L) * Pos(0, k.d / 2 - k.top_chamfer, k.h)).position
        s.dim(side, (0, 0), (q.Y, q.Z), -7, horizontal=False, text=f"{q.Z:.1f} knob top")

    def plate_notes(self, L: Layout) -> list[str]:
        m = self.p.mount
        return [f"Encoder mount at (u {L.u_c:.2f}, v {L.v_c:.2f}): D{m.hole_d:.1f} bushing hole through a "
                f"{m.clamp_t:.1f} clamp section",
                f"   {m.pocket:.1f} square key pocket from below, engraved ring D{m.ring_d:.1f} x "
                f"{m.ring_w:.1f} wide x {m.ring_depth:.1f} deep"]

    def plate_underside_notes(self, L: Layout) -> list[str]:
        m = self.p.mount
        return [f"Encoder body sits in the {m.pocket:.1f} square pocket: its M7 nut on top clamps "
                f"{m.clamp_t:.1f} mm of plate"]

    def base_notes(self, L: Layout) -> list[str]:
        return ["No posts: the KY-040 hangs from the top plate on its M7 nut,",
                "   so the floor under it stays flat"]

    def sheet5_extra(self, s, L: Layout) -> None:
        """Knob half of sheet 05: section through the D-flat, view from below, notes."""
        from macropad_cad.drawings import HID, View, _norm, draw_section, project, section_polys

        k, e = self.p.knob, self.p.enc
        kl = self.knob_local()
        o, sc = (245, 70), 2.0
        # rotate so the D-flat faces +y and cut on the axis: shows the round / flatted bore step
        draw_section(s, section_polys(Rot(0, 0, 90) * kl, 0.0), sc, o, face="#f3eefb", lw=0.7)
        v = View([], [], sc, o, (-k.d / 2, 0, k.d / 2, k.h))
        s.dim(v, (-k.d / 2, 0), (k.d / 2, 0), -7, text=f"D{k.d:.1f}")
        s.dim(v, (k.d / 2, 0), (k.d / 2, k.h), 6, text=f"{k.h:.1f}")
        s.text(o[0], o[1] - 15, "KNOB - SECTION THROUGH THE D-FLAT 2:1", 7.5, ha="center", weight="bold")
        s.text(o[0], o[1] - 20, "(printed top face down, i.e. upside down from this view)", 5.8, ha="center", color=HID)
        below = _norm(project(kl, (0, 0, -500), (0, 1, 0), (0, 0, 0), sc, (0, 0)))
        below.origin = (292, 62)
        s.draw(below, lw=0.5)
        s.label(below, "KNOB - FROM BELOW 2:1", dy=-6)
        tip = self.shaft_tip()
        rb = (e.shaft_d + k.bore_clear) / 2
        flat = e.shaft_flat - e.shaft_d / 2 + k.bore_clear / 2
        s.note(210, 158, [
            f"D{k.d:.0f} x {k.h:.0f}, {k.flutes} flutes, {k.top_chamfer:.1f} top chamfer, "
            f"indicator dimple D{k.dot_d:.1f} at R{k.dot_r:.0f}",
            f"D-bore D{2 * rb:.2f}, {rb + flat:.2f} across the flat; round below the flat so it only grips the D",
            f"Bore ends at the shaft tip ({k.h - tip - k.bore_extra:.1f} under the top): press on until it stops",
            f"Skirt recess D{k.skirt_d:.1f} x {k.skirt_h:.1f} clears the M7 nut, washer and bushing thread",
            f"Rides {k.gap:.1f} above the plate.  Print top face down, 4 walls, 30 % infill, no supports",
            f"Too tight / loose on the shaft: Knob.bore_clear in rotary/cad/edition.py ({k.bore_clear:.2f} now)",
        ], 6.3, title="Knob (rotary edition)")


EDITION = RotaryEdition()
