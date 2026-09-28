#!/usr/bin/env python3
"""Physical wiring maps: where every wire runs, drawn on the real layout.

    python3 tools/wiring_map.py            -> docs/images/wiring_map_joystick.png|svg, wiring_map_rotary.png|svg

Positions come from the CAD model (the editions' layouts) and the pin names from
each edition's firmware/include/config.h, so the maps always match the parts.

Left panel: the top plate seen from BELOW (as it lies on your bench, upside
down, front edge towards you), with the switches, diodes, row and column wires,
the display and centre-control headers.  Right panel: the base seen from above
with the Pico and the two LED sticks.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Circle, FancyBboxPatch, Polygon, Rectangle  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
from editions import EDITIONS, REPO, layouts  # noqa: E402
from wiring_diagram import COL, parse_config  # noqa: E402

OUT = REPO / "docs" / "images"
FONT = "DejaVu Sans"
ROW_COLORS = ["#1d4ed8", "#2563eb", "#3b82f6", "#60a5fa"]
COL_COLORS = ["#15803d", "#16a34a", "#22c55e"]
INK = "#1f2937"
GHOST = "#9ca3af"
PAD = "#e5e7eb"


def rounded(ax, x, y, w, h, r, **kw):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle=f"round,pad=0,rounding_size={r}", **kw))


# ------------------------------------------------------------- plate panel
def plate_panel(ax, L, v, rotary: bool):
    """Underside view: u is mirrored (u' = W - u) so left/right match the bench."""
    W, V = L.W, L.V
    mx = lambda u: W - u  # noqa: E731

    ax.set_xlim(-16, W + 16)
    ax.set_ylim(-14, V + 18)
    ax.set_aspect("equal")
    ax.axis("off")
    rounded(ax, 0, 0, W, V, 7, fc="#fafafa", ec=INK, lw=1.4)
    ax.text(W / 2, V + 12, "TOP PLATE, seen from BELOW (front edge at the bottom)", ha="center", fontsize=11,
            weight="bold", fontfamily=FONT, color=INK)
    ax.text(W / 2, -10, "left / right are mirrored compared to the top view: the bar display is on the RIGHT here",
            ha="center", fontsize=7.5, fontfamily=FONT, color="#6b7280")

    # bosses
    for _, x, y in L.bosses:
        ax.add_patch(Circle((mx(x), y / L.cos), 4.0, fc="#f3f4f6", ec=GHOST, lw=0.8))

    # displays (pocket footprints) and their headers
    d, b = L.p.d19, L.p.bar
    u_hdr = L.u_c + d.aa_x0 + d.aa_l / 2  # the module's header end sits here (right in the top view)
    x0, x1 = mx(u_hdr), mx(u_hdr - d.pcb_l)  # ... which is the LEFT end in the underside view
    y0, y1 = L.v_w - d.pcb_w / 2, L.v_w + d.pcb_w / 2
    ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc="#eef2ff", ec="#6366f1", lw=1.0))
    ax.text((x0 + x1) / 2 + 4, y0 + 7.5, '1.9" display (back of the module)\nheader: GND VCC SCL SDA RES DC CS BLK -> Pico',
            ha="center", va="center", fontsize=6.8, fontfamily=FONT, color="#4338ca")
    hx = x0 + d.header_x  # header pins along the left short edge (underside view)
    for i in range(8):
        ax.add_patch(Circle((hx, L.v_w - 8.9 + i * 2.54), 0.7, fc="#c7d2fe", ec="#4338ca", lw=0.5))

    bx0, bx1 = mx(L.u_bar + b.pcb_w / 2), mx(L.u_bar - b.pcb_w / 2)
    by0, by1 = L.v_bar + b.aa_cx - b.pcb_l, L.v_bar + b.aa_cx  # PCB from the hole end (back) towards the front
    ax.add_patch(Rectangle((bx0, by0), bx1 - bx0, by1 - by0, fc="#ecfeff", ec="#0891b2", lw=1.0))
    ax.text((bx0 + bx1) / 2, by1 - 12, '2.25"\nbar\n(back)', ha="center", va="center", fontsize=7,
            fontfamily=FONT, color="#0e7490")
    for i in range(8):
        ax.add_patch(Circle((bx0 + 1.6 + i * 2.54, by0 + 2.2), 0.7, fc="#a5f3fc", ec="#0e7490", lw=0.5))
    ax.text((bx0 + bx1) / 2, by0 + 7, "8 pins\n-> Pico", ha="center", va="center", fontsize=6.2, fontfamily=FONT,
            color="#0e7490")

    # centre control
    uc, vc = mx(L.u_c), L.v_c
    if rotary:
        e = L.ed.p.enc
        ax.add_patch(Circle((uc, vc), L.ed.p.mount.hole_d / 2, fc="#fff7ed", ec="#ea580c", lw=1.0))
        py0 = vc - e.axis_dy - e.pcb_w / 2
        ax.add_patch(Rectangle((uc + e.axis_dx - e.pcb_l / 2, py0), e.pcb_l, e.pcb_w, fc="none", ec="#ea580c", lw=0.9,
                               ls="--"))
        ax.text(uc + e.axis_dx, vc - e.axis_dy - 6.5, "KY-040 encoder", ha="center", fontsize=6.3, fontfamily=FONT,
                color="#c2410c")
        hx = uc + e.axis_dx + e.pcb_l / 2 + 1.5  # header on the module's -x edge = right side in the underside view
        for i in range(5):
            ax.add_patch(Circle((hx, vc - e.axis_dy + (i - 2) * 2.54), 0.7, fc="#fed7aa", ec="#c2410c", lw=0.5))
        ax.text(hx + 2.5, vc - e.axis_dy, "header: GND + SW DT CLK", va="center", fontsize=6.5, fontfamily=FONT,
                color="#c2410c")
    else:
        j = L.ed.p.joy
        ax.add_patch(Circle((uc, vc), L.ed.p.mount.hole_d / 2, fc="#fff7ed", ec="#ea580c", lw=1.0))
        ax.add_patch(Rectangle((uc - j.pcb_l / 2, vc + j.pcb_cy - j.pcb_w / 2), j.pcb_l, j.pcb_w, fc="none", ec="#ea580c",
                               lw=0.9, ls="--"))
        ax.text(uc, vc + 6, "KY-023 joystick\n(stands on the base)", ha="center", va="center", fontsize=6.3,
                fontfamily=FONT, color="#c2410c")
        hx = uc + j.pcb_l / 2 + 1.5  # header on the -x edge -> right side here
        for i in range(5):
            ax.add_patch(Circle((hx, vc + j.pcb_cy + (i - 2) * 2.54), 0.7, fc="#fed7aa", ec="#c2410c", lw=0.5))
        ax.text(hx + 2.5, vc + j.pcb_cy, "header: GND +5V VRx VRy SW", va="center", fontsize=6.5, fontfamily=FONT,
                color="#c2410c")

    # switches: body, pins, diodes, row and column wires
    sw = L.p.sw
    cols = sorted({k.u for k in L.keys})  # u_L, u_R1, u_R2
    rows = L.rows
    pinA = (-3.81, 2.54)  # MX contact pins (top view), the diode goes on pin A
    pinB = (2.54, 5.08)
    row_bus_dv = -8.3  # row wire runs this far below the switch centres

    def pin(u, v, p):
        return mx(u + p[0]), v + p[1]  # mirrored

    n_rows = len(rows)
    fw_row = lambda cad_row: n_rows - 1 - cad_row  # noqa: E731  firmware row 0 = back row; the CAD counts from the front

    for k in L.keys:
        r = fw_row(k.row)
        cx, cy = mx(k.u), k.v
        ax.add_patch(Rectangle((cx - sw.hole / 2, cy - sw.hole / 2), sw.hole, sw.hole, fc="#ffffff", ec=INK, lw=0.9))
        ax.text(cx, cy - 4.6, f"K{r}{k.col}", ha="center", fontsize=6.5, fontfamily=FONT, color="#6b7280")
        for p, col in ((pinA, ROW_COLORS[r]), (pinB, COL_COLORS[k.col])):
            x, y = pin(k.u, k.v, p)
            ax.add_patch(Circle((x, y), 1.0, fc="#fde68a", ec="#92400e", lw=0.6))
        # diode: from pin A down to the row bus, band (cathode) at the row end
        ax_, ay_ = pin(k.u, k.v, pinA)
        by = k.v + row_bus_dv
        ax.plot([ax_, ax_], [ay_, by], color=ROW_COLORS[r], lw=1.2, zorder=3)
        dy0, dy1 = ay_ - 3.2, ay_ - 7.0
        ax.add_patch(Rectangle((ax_ - 1.1, dy1), 2.2, dy0 - dy1, fc="#fef3c7", ec="#92400e", lw=0.7, zorder=4))
        ax.add_patch(Rectangle((ax_ - 1.1, dy1), 2.2, 0.9, fc="#111827", ec="none", zorder=5))  # band
        ax.add_patch(Circle((ax_, by), 0.7, color=ROW_COLORS[r], zorder=6))

    # row buses: left column -> across the centre -> right block
    uL, uR1, uR2 = cols
    for cad_r, v_row in enumerate(rows):
        r = fw_row(cad_r)
        col = ROW_COLORS[r]
        y = v_row + row_bus_dv
        xa = mx(uL + pinA[0])  # left column key (on the RIGHT in the underside view)
        xb = mx(uR2 + pinA[0])  # far right key (on the LEFT here)
        # the second row from the front runs into the centre module: detour behind it
        if cad_r == 1:
            det = vc - L.ed.p.enc.axis_dy + L.ed.p.enc.pcb_w / 2 + 5 if rotary else vc + L.ed.p.joy.pcb_cy + L.ed.p.joy.pcb_w / 2 + 6
            xl, xr = mx(L.u_c + 24), mx(L.u_c - 24)
            path = [(xb, y), (xl, y), (xl, det), (xr, det), (xr, y), (xa, y)]
        else:
            path = [(xb, y), (xa, y)]
        xs, ys = zip(*path)
        ax.plot(xs, ys, color=col, lw=1.8, zorder=2, solid_capstyle="round")
        ax.text(xb - 3.0, y, f"ROW{r} GP{v['ROW_PINS'][r]}", ha="right", va="center", fontsize=6.8, fontfamily=FONT,
                color=col, weight="bold")

    # column wires: down each column on pin B, then to the back edge towards the Pico
    for c, u in enumerate(cols):
        col = COL_COLORS[c]
        x = mx(u + pinB[0])
        y_top = rows[-1] + pinB[1]
        y_bot = rows[0] + pinB[1]
        ax.plot([x, x], [y_bot, y_top], color=col, lw=1.8, zorder=2)
        ax.plot([x, x], [y_top, V - 6], color=col, lw=1.2, ls=(0, (3, 2)), zorder=2)
        ax.text(x + (2.5 if c else -2.5), V - 4, f"COL{c} GP{v['COL_PINS'][c]}", ha="left" if c else "right", fontsize=6.8,
                fontfamily=FONT, color=col, weight="bold")

    # Pico ghost at the back centre and the bundle
    px = mx(L.pico_x)
    ax.add_patch(Rectangle((px - 10.5, V - 0.5), 21, 10, fc="none", ec=GHOST, lw=0.9, ls=":"))
    ax.text(px, V + 5, "Pico 2 (in the base, USB at the back edge)", ha="center", fontsize=6.5, fontfamily=FONT,
            color="#6b7280")


# -------------------------------------------------------------- base panel
def base_panel(ax, L, v):
    W, D = L.W, L.D
    ax.set_xlim(-16, W + 16)
    ax.set_ylim(-14, D + 18)
    ax.set_aspect("equal")
    ax.axis("off")
    rounded(ax, 0, 0, W, D, 7, fc="#fafafa", ec=INK, lw=1.4)
    ax.text(W / 2, D + 12, "BASE, seen from ABOVE (front edge at the bottom)", ha="center", fontsize=11, weight="bold",
            fontfamily=FONT, color=INK)
    for _, x, y in L.bosses:
        ax.add_patch(Circle((x, y), 1.7, fc="#ffffff", ec=GHOST, lw=0.8))

    pc = L.p.pico
    px, py1 = L.pico_x, L.pico_edge_y
    py0 = py1 - pc.length
    ax.add_patch(Rectangle((px - pc.width / 2, py0), pc.width, pc.length, fc="#fee2e2", ec="#b91c1c", lw=1.2))
    ax.add_patch(Rectangle((px - 4.5, py1 - 1), 9, 5, fc="#d1d5db", ec="#6b7280", lw=0.7))
    ax.text(px, D + 4.5, "USB-C", ha="center", fontsize=7, fontfamily=FONT, color="#374151")
    ax.text(px, (py0 + py1) / 2, "Pico 2\n(top view)", ha="center", va="center", fontsize=8, weight="bold",
            fontfamily=FONT, color="#7f1d1d")
    # pins 1..20 on the left (x-), 21..40 on the right; pin 1 at the USB end
    pitch = pc.pin_pitch
    for i in range(20):
        y = py1 - pc.first_pin - i * pitch
        ax.add_patch(Circle((px - pc.row_spacing / 2, y), 0.55, fc="#fde68a", ec="#92400e", lw=0.4))
        ax.add_patch(Circle((px + pc.row_spacing / 2, y), 0.55, fc="#fde68a", ec="#92400e", lw=0.4))
    labels = [(40, "VBUS", "-> 1N4001 -> LEDs 5V", "#dc2626"), (38, "GND", "LEDs (+ encoder)", "#111827"),
              (36, "3V3", "displays, stick/encoder", "#dc2626"), (34, "GP28", "LED DIN (330 R)", "#c026d3"),
              (33, "AGND", "joystick GND", "#111827")]
    for pin, name, what, col in labels:
        i = 40 - pin
        y = py1 - pc.first_pin - i * pitch
        x = px + pc.row_spacing / 2
        ax.plot([x + 1, x + 4.5], [y, y], color=col, lw=0.9)
        ax.text(x + 5.2, y, f"{pin}", va="center", fontsize=5.6, fontfamily=FONT, color=col)
    ax.text(4, 88, "Pico pins used from the base:", fontsize=6.2, fontfamily=FONT, color=INK, weight="bold")
    for n, (pin, name, what, col) in enumerate(labels):
        ax.text(4, 84.5 - n * 3.2, f"{pin} {name}  {what}", fontsize=5.7, fontfamily=FONT, color=col)

    s = L.p.led
    for name, loc in L.led_locs.items():
        cx, cy = loc.position.X, loc.position.Y
        ax.add_patch(Rectangle((cx - s.pcb_w / 2, cy - s.pcb_l / 2), s.pcb_w, s.pcb_l, fc="#111827", ec="#111827", lw=0.8))
        din_back = name == "led R"
        for i in range(s.led_n):
            yy = cy + (i - (s.led_n - 1) / 2) * s.led_pitch
            ax.add_patch(Rectangle((cx - s.led_size / 2 + (s.led_y if din_back else -s.led_y) * 0.4, yy - s.led_size / 2),
                                   s.led_size, s.led_size, fc="#f3f4f6", ec="#6b7280", lw=0.4))
        sign = 1 if din_back else -1
        ax.text(cx, cy + sign * (s.pcb_l / 2 + 3), "DIN end", ha="center", fontsize=6.5, fontfamily=FONT, color="#c026d3",
                weight="bold")
        ax.text(cx, cy - sign * (s.pcb_l / 2 + 3), "DOUT end", ha="center", fontsize=6.5, fontfamily=FONT, color="#6b7280")
        for hx, hy in [(cx + (-1 if din_back else 1) * 0 + (s.hole_y if din_back else -s.hole_y),
                        cy + sx * s.hole_pitch / 2) for sx in (-1, 1)]:
            ax.add_patch(Circle((hx, hy), 1.25, fc="#fafafa", ec="#6b7280", lw=0.6))
    # wires: Pico -> right stick DIN end (back), right DOUT (front) -> left DIN (front)
    xr = L.pico_x + L.p.case.led_dx
    xl = L.pico_x - L.p.case.led_dx
    y_back = L.p.case.led_y + s.pcb_l / 2 + 4
    y_front = L.p.case.led_y - s.pcb_l / 2 - 4
    ax.plot([px + pc.row_spacing / 2 + 5, xr - 6], [py1 - pc.first_pin - 4 * pitch, y_back - 1], color="#c026d3", lw=1.3)
    ax.text(xr + 7, y_back - 1, "3 wires from the Pico:\n5V (1N4001), GND,\nDIN (330 R)", va="center",
            fontsize=6.3, fontfamily=FONT, color="#c026d3")
    ax.plot([xr, xr, xl, xl], [y_front - 2, y_front - 6, y_front - 6, y_front - 2], color="#c026d3", lw=1.3)
    ax.text(L.pico_x, y_front - 9.5, "jumper, ~70 mm: 5V, GND, right DOUT -> left DIN", ha="center", fontsize=6.3,
            fontfamily=FONT, color="#c026d3")
    ax.text(W / 2, -10, "The plate's wire bundle (rows, columns, displays, centre control) comes down to the Pico with ~8 cm of slack",
            ha="center", fontsize=7.5, fontfamily=FONT, color="#6b7280")


def draw(name: str, L):
    v = parse_config(REPO / name / "firmware" / "include" / "config.h")
    fig = plt.figure(figsize=(17, 8.2))
    ax1 = fig.add_axes([0.01, 0.02, 0.56, 0.96])
    ax2 = fig.add_axes([0.58, 0.02, 0.41, 0.96])
    plate_panel(ax1, L, v, rotary=(name == "rotary"))
    base_panel(ax2, L, v)
    fig.text(0.5, 0.005, f"{name} edition  -  generated from the CAD layout and {name}/firmware/include/config.h",
             ha="center", fontsize=7, color="#9ca3af", fontfamily=FONT)
    for ext in ("png", "svg"):
        fig.savefig(OUT / f"wiring_map_{name}.{ext}", dpi=150 if ext == "png" else None, facecolor="white")
    plt.close(fig)
    print("wrote", OUT / f"wiring_map_{name}.png")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for name, L in layouts().items():
        if name in (sys.argv[1:] or EDITIONS):
            draw(name, L)
