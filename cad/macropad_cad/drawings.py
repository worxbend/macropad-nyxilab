"""Technical drawings ("blueprints") generated from the same model.

Hidden-line projections come from OCCT (``project_to_viewport``); sheets,
dimensions and title blocks are drawn with matplotlib.  Output:

  exports/drawings/nyxilab_macropad_blueprints.pdf   all A3 sheets
  exports/drawings/sheet_XX_*.svg                    one SVG per sheet
  exports/drawings/template_top_plate_1to1.pdf       A4, print at 100 % to check parts on paper
"""

from __future__ import annotations

import datetime
import math
from dataclasses import dataclass, field
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.backends.backend_pdf import PdfPages  # noqa: E402
from matplotlib.patches import FancyArrowPatch, PathPatch, Rectangle  # noqa: E402
from matplotlib.path import Path as MPath  # noqa: E402

from build123d import Plane, Pos  # noqa: E402
from build123d import Rectangle as BRect  # noqa: E402

from . import components, parts  # noqa: E402
from .layout import L as DEFAULT_LAYOUT  # noqa: E402
from .layout import Layout  # noqa: E402

A3 = (420.0, 297.0)
A4 = (297.0, 210.0)
MM = 1 / 25.4
INK = "#1d1f24"
DIM = "#1f4e9c"
HID = "#8a8f98"
ACCENT = "#5B3E8E"
FONT = "DejaVu Sans"


def _edge_polyline(e, seg: float = 0.6):
    try:
        if e.geom_type.name == "LINE":
            pts = [e.start_point(), e.end_point()]
        else:
            n = max(6, min(90, int(e.length / seg) + 1))
            pts = [e.position_at(i / n) for i in range(n + 1)]
    except Exception:
        return None
    return [(p.X, p.Y) for p in pts]


@dataclass
class View:
    """A projected view placed on a sheet: sheet = origin + scale * view."""

    visible: list
    hidden: list
    scale: float
    origin: tuple[float, float]
    bounds: tuple[float, float, float, float] = field(default=(0, 0, 0, 0))

    def to_sheet(self, x: float, y: float) -> tuple[float, float]:
        return self.origin[0] + x * self.scale, self.origin[1] + y * self.scale


def project(shape, eye, up, look_at, scale, origin, show_hidden=True) -> View:
    vis, hid = shape.project_to_viewport(eye, up, look_at)
    vl = [p for p in (_edge_polyline(e) for e in vis) if p]
    hl = [p for p in (_edge_polyline(e) for e in hid) if p] if show_hidden else []
    xs = [x for pl in vl for x, _ in pl]
    ys = [y for pl in vl for _, y in pl]
    return View(vl, hl, scale, origin, (min(xs), min(ys), max(xs), max(ys)))


class Sheet:
    def __init__(self, title: str, subtitle: str, number: int, size=A3, scale_note: str = "1:1"):
        self.size = size
        self.fig = plt.figure(figsize=(size[0] * MM, size[1] * MM))
        self.ax = self.fig.add_axes([0, 0, 1, 1])
        self.ax.set_xlim(0, size[0])
        self.ax.set_ylim(0, size[1])
        self.ax.set_aspect("equal")
        self.ax.axis("off")
        self.title, self.subtitle, self.number, self.scale_note = title, subtitle, number, scale_note
        self._frame()

    # ------------------------------------------------------------ chrome
    def _frame(self):
        w, h = self.size
        ax = self.ax
        ax.add_patch(Rectangle((10, 10), w - 20, h - 20, fill=False, lw=1.2, ec=INK))
        bw, bh = 170, 34
        x0, y0 = w - 10 - bw, 10
        ax.add_patch(Rectangle((x0, y0), bw, bh, fill=False, lw=1.0, ec=INK))
        for yy in (y0 + 12, y0 + 22):
            ax.plot([x0, x0 + bw], [yy, yy], color=INK, lw=0.5)
        ax.plot([x0 + 110, x0 + 110], [y0, y0 + 22], color=INK, lw=0.5)
        ax.add_patch(Rectangle((x0, y0 + 22), 6, 12, color=ACCENT, lw=0))
        self.text(x0 + 9, y0 + 28, "NYXILAB MACROPAD", 9, weight="bold")
        self.text(x0 + 3, y0 + 16.5, self.title, 8.5, weight="bold")
        self.text(x0 + 3, y0 + 5.5, self.subtitle, 6.5)
        self.text(x0 + 113, y0 + 16.5, f"SHEET {self.number:02d}   SCALE {self.scale_note}", 6.5)
        self.text(x0 + 113, y0 + 5.5, f"mm  |  {datetime.date.today().isoformat()}", 6.5)
        self.text(12, 12.5, "Generated from cad/macropad_cad (build123d). Do not scale the PDF - "
                  "use the dimensions or the STEP files.", 5.5, color=HID)

    def text(self, x, y, s, size=7, color=INK, ha="left", va="center", rot=0, weight="normal"):
        self.ax.text(x, y, s, fontsize=size, color=color, ha=ha, va=va, rotation=rot, fontfamily=FONT,
                     fontweight=weight)

    # ------------------------------------------------------------- views
    def draw(self, v: View, lw=0.7, hidden_lw=0.35):
        for pl in v.hidden:
            xs, ys = zip(*(v.to_sheet(*p) for p in pl))
            self.ax.plot(xs, ys, color=HID, lw=hidden_lw, ls=(0, (2.2, 1.6)), solid_capstyle="round")
        for pl in v.visible:
            xs, ys = zip(*(v.to_sheet(*p) for p in pl))
            self.ax.plot(xs, ys, color=INK, lw=lw, solid_capstyle="round")

    def label(self, v: View, s: str, dy=-9):
        x0, y0, x1, _ = v.bounds
        cx, cy = v.to_sheet((x0 + x1) / 2, y0)
        self.text(cx, cy + dy, s, 7.5, ha="center", weight="bold")

    # -------------------------------------------------------- dimensions
    def _arrow(self, a, b):
        self.ax.add_patch(FancyArrowPatch(a, b, arrowstyle="<|-|>", mutation_scale=5, lw=0.45, color=DIM,
                                          shrinkA=0, shrinkB=0))

    def dim(self, v: View, p1, p2, off: float, text: str | None = None, horizontal: bool | None = None, size=6):
        """Linear dimension between view points p1, p2, offset ``off`` mm on the sheet."""
        a, b = v.to_sheet(*p1), v.to_sheet(*p2)
        if horizontal is None:
            horizontal = abs(b[0] - a[0]) >= abs(b[1] - a[1])
        if horizontal:
            y = (a[1] if off > 0 else a[1]) + off
            y = max(a[1], b[1]) + off if off > 0 else min(a[1], b[1]) + off
            for p in (a, b):
                self.ax.plot([p[0], p[0]], [p[1] + math.copysign(1.0, off), y + math.copysign(1.2, off)], color=DIM, lw=0.35)
            self._arrow((a[0], y), (b[0], y))
            val = abs(p2[0] - p1[0])
            self.text((a[0] + b[0]) / 2, y + math.copysign(1.8, off) if off > 0 else y - 1.8, text or f"{val:.2f}",
                      size, color=DIM, ha="center")
        else:
            x = max(a[0], b[0]) + off if off > 0 else min(a[0], b[0]) + off
            for p in (a, b):
                self.ax.plot([p[0] + math.copysign(1.0, off), x + math.copysign(1.2, off)], [p[1], p[1]], color=DIM, lw=0.35)
            self._arrow((x, a[1]), (x, b[1]))
            val = abs(p2[1] - p1[1])
            self.text(x + (1.8 if off > 0 else -1.8), (a[1] + b[1]) / 2, text or f"{val:.2f}", size, color=DIM,
                      ha="center", rot=90)

    def ordinate(self, v: View, values, axis: str, base: float, edge: float, off: float, size=5.5, fmt="{:.2f}"):
        """Ordinate dimensions: ``values`` measured from ``base`` along x or y.

        ``edge`` is the view coordinate (other axis) where extension lines start,
        ``off`` is the sheet offset to the text line.
        """
        seen = []
        for val in sorted(values):
            if any(abs(val - s) < 0.01 for s in seen):
                continue
            seen.append(val)
            if axis == "x":
                p = v.to_sheet(val, edge)
                y1 = p[1] + off
                self.ax.plot([p[0], p[0]], [p[1], y1], color=DIM, lw=0.3)
                self.text(p[0], y1 + (1.2 if off > 0 else -1.2), fmt.format(val - base), size, color=DIM,
                          ha="center", va="bottom" if off > 0 else "top", rot=90)
            else:
                p = v.to_sheet(edge, val)
                x1 = p[0] + off
                self.ax.plot([p[0], x1], [p[1], p[1]], color=DIM, lw=0.3)
                self.text(x1 + (1.0 if off > 0 else -1.0), p[1], fmt.format(val - base), size, color=DIM,
                          ha="left" if off > 0 else "right")

    def note(self, x, y, lines, size=6.2, title=None):
        if title:
            self.text(x, y, title, size + 1.2, weight="bold")
            y -= 5
        for ln in lines:
            self.text(x, y, ln, size)
            y -= size * 0.62
        return y

    def table(self, x, y, rows, widths, size=6, header=True):
        rh = 4.6
        for r, row in enumerate(rows):
            cx = x
            for c, cell in enumerate(row):
                self.text(cx + 1.2, y - r * rh - rh / 2, str(cell), size, weight="bold" if header and r == 0 else "normal")
                cx += widths[c]
            self.ax.plot([x, x + sum(widths)], [y - (r + 1) * rh] * 2, color=INK, lw=0.6 if r == 0 else 0.25)
        self.ax.plot([x, x + sum(widths)], [y, y], color=INK, lw=0.6)
        return y - len(rows) * rh

    def image(self, path, x, y, w):
        img = plt.imread(str(path))
        h = w * img.shape[0] / img.shape[1]
        self.ax.imshow(img, extent=(x, x + w, y, y + h), zorder=0)
        return h


# ================================================================ sections
def _wire_pts(w, seg=0.4):
    n = max(12, min(400, int(w.length / seg)))
    return [w.position_at(i / n) for i in range(n)]


def section_polys(shape, x: float):
    """Cross-section of ``shape`` by the plane X = x, as (y, z) polygons with holes."""
    cut = Plane.YZ.offset(x) * BRect(2000, 2000)
    try:
        sec = shape & cut
    except Exception:
        return []
    out = []
    for f in sec.faces():
        outer = [(q.Y, q.Z) for q in _wire_pts(f.outer_wire())]
        holes = [[(q.Y, q.Z) for q in _wire_pts(w)] for w in f.inner_wires()]
        out.append((outer, holes))
    return out


def _signed_area(pts):
    return 0.5 * sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]))


def draw_section(sheet: "Sheet", polys, scale, origin, face="#ffffff", edge=INK, hatch="////", lw=0.6, alpha=1.0):
    for outer, holes in polys:
        verts, codes = [], []
        for ring, want_ccw in [(outer, True)] + [(h, False) for h in holes]:
            ring = ring if (_signed_area(ring) > 0) == want_ccw else ring[::-1]
            pts = [(origin[0] + y * scale, origin[1] + z * scale) for y, z in ring]
            verts += pts + [pts[0]]
            codes += [MPath.MOVETO] + [MPath.LINETO] * (len(pts) - 1) + [MPath.CLOSEPOLY]
        patch = PathPatch(MPath(verts, codes), facecolor=face, edgecolor=edge, lw=lw, hatch=hatch, alpha=alpha)
        sheet.ax.add_patch(patch)


# ================================================================== sheets
def _world_views(shape, L: Layout, scale, top_origin, front_origin, side_origin, hidden=True):
    """Third-angle layout: top view, front view (from -Y) below it, right side view beside the front."""
    top = project(shape, (L.W / 2, L.D / 2, 500), (0, 1, 0), (L.W / 2, L.D / 2, 0), scale, top_origin, hidden)
    front = project(shape, (L.W / 2, -500, 0), (0, 0, 1), (L.W / 2, 0, 0), scale, front_origin, hidden)
    side = project(shape, (500, L.D / 2, 0), (0, 0, 1), (0, L.D / 2, 0), scale, side_origin, hidden)
    return top, front, side


def _norm(v: View):
    """Shift a view so its visible bounds start at (0, 0) in view coordinates."""
    x0, y0, x1, y1 = v.bounds
    v.visible = [[(x - x0, y - y0) for x, y in pl] for pl in v.visible]
    v.hidden = [[(x - x0, y - y0) for x, y in pl] for pl in v.hidden]
    v.bounds = (0, 0, x1 - x0, y1 - y0)
    return v


def sheet_overview(L: Layout, render_png: Path | None) -> Sheet:
    c = L.p.case
    s = Sheet("GENERAL ARRANGEMENT", f"{L.W:.1f} x {L.D:.1f} mm, {c.tilt_deg:.0f} deg wedge, 3-layer stack", 1,
              scale_note="1:1.5 / A-A 1:1")
    asm = parts.top_plate(L) + parts.frame(L) + parts.base(L)
    sc = 1 / 1.5
    top = _norm(project(parts.top_plate(L), (L.W / 2, L.D / 2, 500), (0, 1, 0), (L.W / 2, L.D / 2, 0), sc, (0, 0),
                        show_hidden=False))
    front = _norm(project(asm, (L.W / 2, -500, 0), (0, 0, 1), (L.W / 2, 0, 0), sc, (0, 0), show_hidden=False))
    side = _norm(project(asm, (500, L.D / 2, 0), (0, 0, 1), (0, L.D / 2, 0), sc, (0, 0), show_hidden=False))
    top.origin = (30, 196)
    front.origin = (30, 150)
    side.origin = (30 + L.W * sc + 18, 150)
    for v in (top, front, side):
        s.draw(v)
    s.label(top, "TOP VIEW", dy=-5)
    s.label(front, "FRONT VIEW (user side)", dy=-7)
    s.label(side, "RIGHT SIDE VIEW", dy=-7)
    s.dim(top, (0, 0), (L.W, 0), -9)
    s.dim(top, (L.W, 0), (L.W, L.D), 6)
    s.dim(front, (0, 0), (0, L.H_f), -6, text=f"{L.H_f:.1f}")
    s.dim(side, (L.D, 0), (L.D, L.H_b), 6, text=f"{L.H_b:.1f}")
    ax_ = side.to_sheet(L.D * 0.45, L.H_f + L.D * 0.45 * L.tan + 5)
    s.text(ax_[0], ax_[1], f"{c.tilt_deg:.0f} deg", 6.5, color=DIM, ha="center")
    # cutting-plane marks on the top view
    for yy in (-4, L.D + 4):
        a = top.to_sheet(L.u_c, yy)
        s.text(a[0], a[1], "A", 7, color=ACCENT, ha="center", weight="bold")
    a0, a1 = top.to_sheet(L.u_c, -1.5), top.to_sheet(L.u_c, L.D + 1.5)
    s.ax.plot([a0[0], a1[0]], [a0[1], a1[1]], color=ACCENT, lw=0.5, ls=(0, (6, 2, 1, 2)))

    # section A-A at 1.5:1 through the joystick, main display, Pico and USB port
    o, k = (28, 34), 1.5
    comp = components.placed_components(L)
    styles = [
        (comp["joystick"]["pcb"], "#d9d9d9", "xxxx"), (comp["joystick"]["mech"], "#e6e6e6", ""),
        (comp["joystick"]["knob"], "#cfcfcf", ""), (comp["display 1.9"]["lcm"], "#d0d0d0", ""),
        (comp["display 1.9"]["pcb"], "#d9d9d9", "xxxx"), (comp["display 1.9"]["back"], "#e6e6e6", ""),
        (comp["pico"]["pcb"], "#d9d9d9", "xxxx"), (comp["pico"]["usb"], "#e6e6e6", ""),
    ]
    for shp, face, hatch in styles:
        draw_section(s, section_polys(shp, L.u_c), k, o, face=face, edge="#555a63", hatch=hatch, lw=0.4)
    for name in ("top_plate", "frame", "base"):
        draw_section(s, section_polys(parts.all_parts(L)[name], L.u_c), k, o,
                     face="#f3eefb" if name == "frame" else "#ffffff", lw=0.7)
    s.text(o[0] + L.D * k / 2, o[1] - 6, f"SECTION A-A  (x = {L.u_c:.2f}, 1.5:1)", 7.5, ha="center", weight="bold")
    call = [
        ((L.v_j * L.cos + 16, L.H_f + 21), "joystick knob"),
        ((L.v_w * L.cos, L.z_top(L.v_w * L.cos) + 4), "1.9in display"),
        ((L.pico_edge_y - 26, L.pico_z - 5.5), "Pico 2 on standoffs"),
        ((L.D - 5, L.usb_center.Z + 7.5), "USB-C"),
        ((8, L.H_f + 4), "top plate"),
        ((L.D - 8, L.H_b - 12), "frame"),
    ]
    for (yy, zz), txt in call:
        s.text(o[0] + yy * k, o[1] + zz * k, txt, 5.8, color=DIM, ha="center")

    if render_png and Path(render_png).exists():
        s.image(render_png, 250, 168, 160)
    rows = [["#", "Part", "Qty", "Spec / use"],
            ["1", "top_plate (colour A)", "1", "PLA/PETG, top face down"],
            ["2", "frame (colour B)", "1", "PLA/PETG, accent colour"],
            ["3", "base (colour A)", "1", "PLA/PETG"],
            ["4", "bar_spine", "1", "PLA/PETG, 100 %"],
            ["5", "M3x8 countersunk ISO 10642", "8", "case screws"],
            ["6", "M3 heat-set insert 5.7x4.6", "8", "plate bosses"],
            ["7", "M3x6 self-tapping", "4", "joystick posts"],
            ["8", "M2x4 self-tapping", "4", "1.9in display"],
            ["9", "M2x8 self-tapping", "2", "bar display + spine"],
            ["10", "M2x5 self-tapping", "2", "Pico 2 standoffs"],
            ["11", "Rubber feet 10x3", "4", "base recesses"],
            ["12", "MX switch + 1u keycap", "12", "plate mount"],
            ["13", "1N4148 diode", "12", "COL2ROW"]]
    s.table(250, 160, rows, [8, 52, 10, 90], size=5.6)
    return s


def plate_local(L: Layout):
    return L.ploc.inverse() * parts.top_plate(L)


def sheet_top_plate(L: Layout) -> Sheet:
    p, c = L.p, L.p.case
    s = Sheet("TOP PLATE - TRUE VIEW", "Viewed normal to the tilted top surface (plate frame u, v)", 2)
    tl = plate_local(L)
    v = project(tl, (0, 0, 500), (0, 1, 0), (0, 0, 0), 1.0, (40, 70), show_hidden=False)
    s.draw(v)
    s.label(v, "TOP (visible face)", dy=-10)
    # ordinates: columns & features along u, rows along v
    us = [0.0, L.u_bar, L.u_L, L.u_c, L.u_R1, L.u_R2, L.W]
    s.ordinate(v, us, "x", 0.0, L.V, 10)
    vs = [0.0, *L.rows, L.v_j, L.v_w, L.v_bar, L.V]
    s.ordinate(v, vs, "y", 0.0, 0.0, -12)
    s.text(40, 70 + L.V + 38, "Ordinates from the front-left corner of the top surface (u right, v up-slope).",
           6.2, color=DIM)
    d19, bar = p.d19, p.bar
    notes = [
        f"MX cut-outs 12x  {p.sw.hole:.1f} x {p.sw.hole:.1f} through, clip relief {p.sw.pocket_u:.1f} x {p.sw.pocket_v:.1f} "
        f"from below, skin {p.sw.skin:.1f}",
        f"Key pitch {p.sw.pitch:.2f}  | columns u = {L.u_L:.2f} / {L.u_R1:.2f} / {L.u_R2:.2f}",
        f"Rows v = " + " / ".join(f"{r:.2f}" for r in L.rows),
        f"1.9in window {d19.aa_l + 2 * (d19.va_margin + c.window_margin):.2f} x {d19.aa_w + 2 * (d19.va_margin + c.window_margin):.2f}"
        f" centred (u {L.u_c:.2f}, v {L.v_w:.2f}), {c.window_chamfer:.1f} x 45 bevel",
        f"2.25in window {bar.aa_w + 2 * (bar.va_margin + c.window_margin):.2f} x {bar.aa_l + 2 * (bar.va_margin + c.window_margin):.2f}"
        f" centred (u {L.u_bar:.2f}, v {L.v_bar:.2f}), {c.window_chamfer:.1f} x 45 bevel",
        f"Joystick opening D{c.joy_hole_d:.1f} + {c.joy_hole_chamfer:.1f} x 45 bevel at (u {L.u_c:.2f}, v {L.v_j:.2f})",
        f"Plate {c.plate_t:.1f} thick, display skin {c.disp_skin:.1f}, top edge chamfer {c.top_chamfer:.1f}",
        f"Slope length V = {L.V:.2f} (world depth {L.D:.1f} at {c.tilt_deg:.0f} deg)",
    ]
    s.note(232, 270, notes, 6.3, title="Features")
    # underside view (mirror as seen from below)
    vu = project(tl, (0, 0, -500), (0, 1, 0), (0, 0, 0), 0.55, (0, 0), show_hidden=False)
    vu = _norm(vu)
    vu.origin = (240, 110)
    s.draw(vu, lw=0.5)
    s.label(vu, "UNDERSIDE (from below, 1:1.8)", dy=-6)
    s.note(240, 92, [
        "Bosses: world-vertical, M3 heat-set insert D4.0 x 6.2 at the lower end",
        "Display pockets: glass + PCB steps, M2 pilot D1.7 blind (0.7 skin left)",
        "Header relief grooves over the display pin rows (front-side joints)",
        f"Locating lip {c.plate_lip_t:.1f} x {c.plate_lip_h:.1f} drops inside the frame",
    ], 5.8)
    return s


def sheet_frame(L: Layout) -> Sheet:
    c, pc = L.p.case, L.p.pico
    s = Sheet("FRAME (CENTRE LAYER)", "Wedge wall ring, accent colour. USB-C port in the back wall.", 3)
    fr = parts.frame(L)
    top, front, side = (_norm(v) for v in _world_views(fr, L, 1.0, (0, 0), (0, 0), (0, 0)))
    top.origin = (30, 150)
    side.origin = (215, 150)
    s.draw(top)
    s.draw(side)
    s.label(top, "TOP VIEW", dy=-6)
    s.label(side, "RIGHT SIDE VIEW", dy=-8)
    s.dim(top, (0, 0), (L.W, 0), -12)
    s.dim(top, (0, 0), (0, L.D), -10)
    wl = top.to_sheet(c.wall + 2, L.D / 2)
    s.text(wl[0], wl[1], f"<- wall {c.wall:.1f}", 6, color=DIM)
    hb = L.H_b - c.plate_t / L.cos - c.base_t
    hf = L.H_f - c.plate_t / L.cos - c.base_t
    s.dim(side, (0, 0), (0, hf), -8, text=f"{hf:.2f}")
    s.dim(side, (L.D, 0), (L.D, hb), 8, text=f"{hb:.2f}")
    s.dim(side, (0, 0), (L.D, 0), -8)
    # back view with USB port detail
    back = _norm(project(fr, (L.W / 2, 500, 0), (0, 0, 1), (L.W / 2, 0, 0), 1.0, (0, 0)))
    back.origin = (30, 60)
    s.draw(back)
    s.label(back, "BACK VIEW (looking at the USB port)", dy=-8)
    uc = L.usb_center
    ux = L.W - uc.X  # back view is mirrored in X
    uz = uc.Z - c.base_t
    s.dim(back, (0, uz), (ux, uz), 22, horizontal=True, text=f"{L.W - ux:.2f} to centre")
    s.dim(back, (ux + 16, -c.base_t), (ux + 16, uz), 0.1, horizontal=False, text=f"{uc.Z:.2f}", size=5.5)
    lb = back.to_sheet(ux + 19, uz)
    s.text(lb[0], lb[1], "port centre above the desk", 5.5, color=DIM)
    s.note(215, 105, [
        f"Port centre x = {uc.X:.2f} from the left edge, {uc.Z:.2f} above the desk",
        f"Thin wall {c.usb_thin_wall:.1f} with receptacle opening "
        f"{pc.usb_w + 2 * c.usb_open_clear:.2f} x {pc.usb_h + 2 * c.usb_open_clear:.2f} (R{1.2 + c.usb_open_clear:.1f})",
        f"Outer recess {c.usb_recess[0]:.1f} x {c.usb_recess[1]:.1f} R{c.usb_recess[2]:.1f}, "
        f"{c.wall - c.usb_thin_wall:.1f} deep, for the plug overmold",
        "Receptacle face ends flush with the recess floor: any USB-C cable fits,",
        "and the port captures the Pico's USB end (the clone has no holes there).",
        f"Boss saddles: 8x R{L.p.fas.boss_d / 2 + L.p.tol.fit + 0.05:.2f} vertical grooves locate the frame",
        "0.5 mm chamfers at both seams form V-grooves between colours",
    ], 6.3, title="USB-C port & details")
    return s


def sheet_base(L: Layout) -> Sheet:
    c, fas, pc, j = L.p.case, L.p.fas, L.p.pico, L.p.joy
    s = Sheet("BASE (BOTTOM LAYER)", "Pico 2 cradle, joystick posts, M3 countersinks, feet", 4, scale_note="1.3:1")
    b = parts.base(L)
    top = project(b, (L.W / 2, L.D / 2, 500), (0, 1, 0), (L.W / 2, L.D / 2, 0), 1.3, (0, 0), show_hidden=False)
    top = _norm(top)
    top.origin = (40, 95)
    s.draw(top)
    s.label(top, "TOP VIEW 1.3:1 (inside face)", dy=-8)
    bx = [x for _, x, _ in L.bosses]
    by = [y for _, _, y in L.bosses]
    s.ordinate(top, sorted(set(round(x, 2) for x in bx)) + [0.0, L.W], "x", 0.0, L.D, 10, fmt="{:.1f}")
    s.ordinate(top, sorted(set(round(y, 2) for y in by)) + [0.0, L.D], "y", 0.0, 0.0, -10, fmt="{:.1f}")
    ph = [(L.loc_pico * Pos(pc.length - pc.hole_from_end, sy * pc.hole_spacing / 2, 0)).position for sy in (-1, 1)]
    jh = [(L.loc_joy * Pos(hx, hy, -j.pcb_t)).position for hx, hy in L.joy_holes_local]
    lines = [
        f"8x countersunk D{fas.m3_clear_d:.1f} / D{fas.m3_csk_d:.1f} x 90 deg (M3x8 ISO 10642)",
        f"Boss centres {c.boss_inset:.1f} from the edges; see ordinates",
        f"Pico standoffs D4.2 x {pc.standoff_h:.1f}, M2 pilot D{fas.m2_pilot_d:.1f}: "
        + ", ".join(f"({q.X:.2f}, {q.Y:.2f})" for q in ph),
        "Pico centre rail + forward stop (USB end is held by the frame port)",
        f"Joystick posts D7.0, perpendicular to the plate ({c.tilt_deg:.0f} deg), M3 pilot D{fas.m3_pilot_d:.1f}",
        "   top centres: " + ", ".join(f"({q.X:.1f}, {q.Y:.1f}, z{q.Z:.1f})" for q in jh[:2]),
        "                " + ", ".join(f"({q.X:.1f}, {q.Y:.1f}, z{q.Z:.1f})" for q in jh[2:]),
        f"Joystick solder-joint relief {c.joy_base_relief:.1f} deep under the module",
        f"Feet: 4x D{c.feet_d:.1f} x {c.feet_depth:.1f} recesses, {c.feet_inset:.1f} from the edges",
        f"Locating lip {c.base_lip_t:.1f} x {c.base_lip_h:.1f} inside the frame (gaps at bosses & USB)",
    ]
    s.note(262, 205, lines, 6.0, title="Features")
    return s


def sheet_small_parts(L: Layout) -> Sheet:
    s = Sheet("BAR DISPLAY SPINE & FIT TESTS", "Clamp for the 2.25in module; coupons to print first", 5,
              scale_note="2:1")
    sp = parts.bar_spine_local(L)
    top = _norm(project(sp, (0, 0, 500), (0, 1, 0), (0, 0, 0), 2.0, (0, 0)))
    top.origin = (30, 190)
    side = _norm(project(sp, (0, -500, 0), (0, 0, 1), (0, 0, 0), 2.0, (0, 0)))
    side.origin = (30, 120)
    s.draw(top)
    s.draw(side)
    s.label(top, "SPINE - TOP 2:1", dy=-7)
    s.label(side, "SPINE - SIDE 2:1 (bed face down)", dy=-7)
    b = L.p.bar
    s.note(210, 250, [
        "Held by the bar module's own two M2 holes (M2x8 through spine + PCB into the plate).",
        f"Arches {b.back_h + 1.0:.1f} mm over the FPC / ZIF / parts on the module back,",
        f"pad presses the PCB {b.free_back_zone[0]:.0f}-{b.free_back_zone[1]:.0f} mm from the header end (0.2 mm preload).",
        "Print flat side down, 100 % infill.",
    ], 6.3, title="Bar display spine")
    s.note(210, 205, [
        "fit_switch      3 MX holes 13.9 / 14.0 / 14.1 - pick the one your switches click into",
        "fit_plate_d19   1.9in window, pockets and M2 pilots",
        "fit_plate_bar   2.25in window, pockets and pilots",
        "fit_plate_joy   joystick opening and bevel",
        "fit_usb         back wall + Pico cradle: plug a cable in, check alignment",
        "fit_joystick    base patch with the 4 posts: check the KY-023 hole grid",
        "",
        "If a fit is off, change the value in cad/macropad_cad/params.py",
        "and rebuild (make cad).  Values marked MEASURE are the likely suspects.",
    ], 6.3, title="Fit-test coupons (print these first, ~1 h total)")
    return s


def template_1to1(L: Layout, path: Path):
    """A4 landscape, true scale: top plate outline, cut-outs and key centres."""
    fig = plt.figure(figsize=(A4[0] * MM, A4[1] * MM))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, A4[0])
    ax.set_ylim(0, A4[1])
    ax.set_aspect("equal")
    ax.axis("off")
    v = project(plate_local(L), (0, 0, 500), (0, 1, 0), (0, 0, 0), 1.0, (0, 0), show_hidden=False)
    ox, oy = (A4[0] - L.W) / 2, (A4[1] - L.V) / 2 + 8
    for pl in v.visible:
        xs, ys = zip(*((ox + x, oy + y) for x, y in pl))
        ax.plot(xs, ys, color=INK, lw=0.5)
    for k in L.keys:
        ax.plot([ox + k.u - 3, ox + k.u + 3], [oy + k.v] * 2, color=DIM, lw=0.3)
        ax.plot([ox + k.u] * 2, [oy + k.v - 3, oy + k.v + 3], color=DIM, lw=0.3)
    ax.add_patch(Rectangle((15, 12), 50, 10, fill=False, lw=0.6, ec=INK))
    ax.text(40, 17, "50 x 10 mm check box", fontsize=6, ha="center", va="center", fontfamily=FONT)
    ax.text(A4[0] / 2, 8, "PRINT AT 100 % / ACTUAL SIZE - top plate true view (front edge at the bottom)",
            fontsize=7, ha="center", fontfamily=FONT, color=DIM)
    fig.savefig(path)
    plt.close(fig)


class _Canvas:
    """Minimal stand-in for Sheet so draw_section() can paint on a plain figure."""

    def __init__(self, ax):
        self.ax = ax


def section_png(L: Layout, path: Path):
    """Standalone section A-A through the joystick / main display / Pico / USB port."""
    k, m = 9.0, 16
    fig = plt.figure(figsize=((L.D + 2 * m) * k / 100, (L.H_b + 30) * k / 100), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(-m * k, (L.D + m) * k)
    ax.set_ylim(-7 * k, (L.H_b + 23) * k)
    ax.set_aspect("equal")
    ax.axis("off")
    cv = _Canvas(ax)
    comp = components.placed_components(L)
    for shp, face in [(comp["joystick"]["pcb"], "#3a3a3a"), (comp["joystick"]["mech"], "#a3a3a3"),
                      (comp["joystick"]["knob"], "#2b2b2b"), (comp["display 1.9"]["lcm"], "#1c2b4a"),
                      (comp["display 1.9"]["pcb"], "#1f5fa8"), (comp["display 1.9"]["back"], "#555555"),
                      (comp["pico"]["pcb"], "#b5122b"), (comp["pico"]["usb"], "#b8bcc0")]:
        draw_section(cv, section_polys(shp, L.u_c), k, (0, 0), face=face, edge="#222222", hatch="", lw=0.8)
    colors = {"top_plate": "#ECEAE4", "frame": "#5B3E8E", "base": "#E3E0D9"}
    for name, face in colors.items():
        draw_section(cv, section_polys(parts.all_parts(L)[name], L.u_c), k, (0, 0), face=face, edge="#1d1f24",
                     hatch="", lw=1.1)
    labels = [(L.v_j * L.cos + 17, L.H_f + 21, "joystick"), (L.v_w * L.cos, L.z_top(L.v_w * L.cos) + 4.5, "1.9in display"),
              (L.pico_edge_y - 27, L.pico_z - 5.8, "Pico 2 on standoffs"), (L.D + 8.5, L.usb_center.Z - 0.5, "USB-C"),
              (L.D + 8.5, L.H_b - 12, "frame"), (-8.5, 7.5, "frame"), (5, L.H_f + 4.5, "top plate"),
              (30, -4.5, "base")]
    for (yy, zz, txt) in labels:
        ax.text(yy * k, zz * k, txt, fontsize=15, ha="center", va="center", fontfamily=FONT, color=DIM)
    ax.text(L.D / 2 * k, (L.H_b + 18) * k, f"Section A-A  (x = {L.u_c:.1f} mm, front on the left)", fontsize=17,
            ha="center", fontfamily=FONT, color=INK, weight="bold")
    fig.savefig(path, facecolor="white")
    plt.close(fig)


def build_all(out: Path, L: Layout = DEFAULT_LAYOUT, render_png: Path | None = None, verbose=True,
              section_to: Path | None = None):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    sheets = [
        ("01_general_arrangement", lambda: sheet_overview(L, render_png)),
        ("02_top_plate", lambda: sheet_top_plate(L)),
        ("03_frame", lambda: sheet_frame(L)),
        ("04_base", lambda: sheet_base(L)),
        ("05_spine_and_fit_tests", lambda: sheet_small_parts(L)),
    ]
    pdf_path = out / "nyxilab_macropad_blueprints.pdf"
    with PdfPages(pdf_path) as pdf:
        for name, make in sheets:
            sh = make()
            sh.fig.savefig(out / f"sheet_{name}.svg")
            pdf.savefig(sh.fig)
            plt.close(sh.fig)
            if verbose:
                print("  drawing", name)
    template_1to1(L, out / "template_top_plate_1to1.pdf")
    if section_to:
        section_png(L, section_to)
    return pdf_path
