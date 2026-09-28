#!/usr/bin/env python3
"""Step-by-step soldering figures for docs/soldering.md.

    python3 tools/soldering_figures.py   -> docs/images/soldering_diode.png|svg, soldering_row.png|svg

soldering_diode: one switch from below, the diode bent and soldered on, the band
                 pointing to where the row wire will run.
soldering_row:   a row of three switches with the diode legs joined into the row
                 wire, and the column wire down the other pins.
"""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Circle, FancyArrowPatch, Rectangle  # noqa: E402

OUT = Path(__file__).resolve().parents[1] / "docs" / "images"
FONT = "DejaVu Sans"
INK = "#1f2937"
ROW = "#2563eb"
COLW = "#16a34a"
PIN = "#fde68a"
PIN_EDGE = "#92400e"
SOLDER = "#9ca3af"


def switch_from_below(ax, cx, cy, label=True):
    """MX switch bottom: 14 mm body, centre post, two contact pins (mirrored: seen from below)."""
    ax.add_patch(Rectangle((cx - 7, cy - 7), 14, 14, fc="#f9fafb", ec=INK, lw=1.2))
    ax.add_patch(Circle((cx, cy), 2.0, fc="#e5e7eb", ec="#9ca3af", lw=0.8))  # centre post
    for dx in (-5.0, 5.0):
        ax.add_patch(Circle((cx + dx, cy), 0.85, fc="#e5e7eb", ec="#9ca3af", lw=0.6))  # plastic legs (PCB-mount)
    # contact pins, mirrored because we look from below
    a = (cx + 3.81, cy + 2.54)
    b = (cx - 2.54, cy + 5.08)
    for p in (a, b):
        ax.add_patch(Circle(p, 1.05, fc=PIN, ec=PIN_EDGE, lw=0.8, zorder=4))
    if label:
        ax.text(cx + 8.2, a[1], "pin A", va="center", fontsize=7, fontfamily=FONT, color=PIN_EDGE)
        ax.text(b[0], cy + 8.6, "pin B", ha="center", fontsize=7, fontfamily=FONT, color=PIN_EDGE)
    return a, b


def diode(ax, x0, y0, x1, y1, band_at_end=True, lw=1.2):
    """1N4148 body between (x0,y0) and (x1,y1), band at the (x1,y1) end (cathode)."""
    ax.plot([x0, x1], [y0, y1], color="#6b7280", lw=lw, zorder=3)
    mx, my = (x0 + x1) / 2, (y0 + y1) / 2
    dx, dy = x1 - x0, y1 - y0
    length = (dx * dx + dy * dy) ** 0.5
    ux, uy = dx / length, dy / length
    nx, ny = -uy, ux
    bl, bw = 3.4, 1.7  # body
    corners = [(mx - ux * bl / 2 + nx * bw / 2, my - uy * bl / 2 + ny * bw / 2),
               (mx + ux * bl / 2 + nx * bw / 2, my + uy * bl / 2 + ny * bw / 2),
               (mx + ux * bl / 2 - nx * bw / 2, my + uy * bl / 2 - ny * bw / 2),
               (mx - ux * bl / 2 - nx * bw / 2, my - uy * bl / 2 - ny * bw / 2)]
    ax.add_patch(plt.Polygon(corners, fc="#fef3c7", ec=PIN_EDGE, lw=0.8, zorder=4))
    k = 0.32 if band_at_end else -0.32
    bx, by = mx + ux * bl * k, my + uy * bl * k
    band = [(bx + nx * bw / 2, by + ny * bw / 2), (bx + ux * 0.7 + nx * bw / 2, by + uy * 0.7 + ny * bw / 2),
            (bx + ux * 0.7 - nx * bw / 2, by + uy * 0.7 - ny * bw / 2), (bx - nx * bw / 2, by - ny * bw / 2)]
    ax.add_patch(plt.Polygon(band, fc="#111827", ec="none", zorder=5))


def blob(ax, x, y, r=1.5):
    ax.add_patch(Circle((x, y), r, fc=SOLDER, ec="#4b5563", lw=0.6, zorder=6))


def figure_diode():
    fig, axes = plt.subplots(1, 4, figsize=(15, 4.4))
    titles = ["1  Switch from below", "2  Bend the diode", "3  Solder the anode to pin A", "4  The band points to the row wire"]
    for ax, t in zip(axes, titles):
        ax.set_xlim(-14, 14)
        ax.set_ylim(-14, 14)
        ax.set_aspect("equal")
        ax.axis("off")
        ax.set_title(t, fontsize=10, fontfamily=FONT, color=INK, loc="left")

    # 1: the switch
    a, b = switch_from_below(axes[0], 0, 0)
    axes[0].text(0, -10.5, "two metal pins = the contacts\n(round post + plastic legs do nothing)", ha="center", fontsize=7,
                 fontfamily=FONT, color="#4b5563")

    # 2: the diode, straight then bent
    ax = axes[1]
    diode(ax, -9, 8, 3, 8)
    ax.text(-3, 10.6, "1N4148: the black band = cathode", ha="center", fontsize=7, fontfamily=FONT, color="#4b5563")
    ax.text(9, 8, "cathode", va="center", fontsize=7, fontfamily=FONT, color=INK)
    ax.text(-12.5, 8, "anode", va="center", fontsize=7, fontfamily=FONT, color=INK, ha="right")
    ax.add_patch(FancyArrowPatch((-3, 5.5), (-3, 2.5), arrowstyle="-|>", mutation_scale=12, color="#9ca3af", lw=1.0))
    # bent: anode leg short and vertical, cathode leg long, bent 90 deg
    diode(ax, -9, -1.5, -9, -9.0)
    ax.plot([-9, -9, 4], [-9.0, -12, -12], color="#6b7280", lw=1.2)
    ax.text(-4.5, -3.5, "bend the cathode leg 90°\nand keep it long: it becomes\npart of the row wire", fontsize=7,
            fontfamily=FONT, color="#4b5563", va="top")
    ax.text(-9, 0.8, "cut the anode\nleg to ~4 mm", ha="center", fontsize=7, fontfamily=FONT, color="#4b5563")

    # 3: soldered on pin A
    ax = axes[2]
    a, b = switch_from_below(ax, 0, 1, label=False)
    diode(ax, a[0], a[1] - 0.8, a[0], a[1] - 8.6)
    blob(ax, a[0], a[1] - 0.3)
    ax.plot([a[0], a[0], 12], [a[1] - 8.6, a[1] - 11.5, a[1] - 11.5], color="#6b7280", lw=1.2)
    ax.text(7.6, a[1] + 1.8, "solder here", fontsize=6.5, fontfamily=FONT, color=INK)
    ax.text(0, -12.8, "hold the diode with tweezers, heat pin + leg together for ~2 s", ha="center", fontsize=6.8,
            fontfamily=FONT, color="#4b5563")

    # 4: the row wire
    ax = axes[3]
    a, b = switch_from_below(ax, 0, 2, label=False)
    diode(ax, a[0], a[1] - 0.8, a[0], a[1] - 8.6)
    blob(ax, a[0], a[1] - 0.3)
    ax.plot([-13, 13], [a[1] - 10.5, a[1] - 10.5], color=ROW, lw=2.2, zorder=2)
    ax.plot([a[0], a[0]], [a[1] - 8.6, a[1] - 10.5], color="#6b7280", lw=1.2)
    blob(ax, a[0], a[1] - 10.5)
    ax.text(0, a[1] - 12.6, "ROW wire (all diode cathodes of one row)", ha="center", fontsize=7, fontfamily=FONT, color=ROW,
            weight="bold")
    ax.plot([b[0], b[0]], [b[1] + 1, 13], color=COLW, lw=2.2, zorder=2)
    blob(ax, b[0], b[1] + 0.6)
    ax.text(b[0] - 1.5, 11.5, "COLUMN wire\n(pin B)", ha="right", fontsize=7, fontfamily=FONT, color=COLW, weight="bold")
    ax.text(12.5, a[1] - 5, "band\ntowards\nthe row", fontsize=6.8, fontfamily=FONT, color="#4b5563", ha="center", va="center")

    fig.suptitle("One switch, one diode  (COL2ROW: column -> switch -> diode -> row)", fontsize=12, fontfamily=FONT, color=INK)
    fig.tight_layout()
    for ext in ("png", "svg"):
        fig.savefig(OUT / f"soldering_diode.{ext}", dpi=150 if ext == "png" else None, facecolor="white")
    plt.close(fig)
    print("wrote", OUT / "soldering_diode.png")


def figure_row():
    fig, ax = plt.subplots(figsize=(13, 6.2))
    ax.set_xlim(-8, 70)
    ax.set_ylim(-16, 30)
    ax.set_aspect("equal")
    ax.axis("off")
    pitch = 19.05
    xs = [0, pitch, 2 * pitch]
    for i, x in enumerate(xs):
        for r, y in enumerate((16, -2)):
            a, b = switch_from_below(ax, x, y, label=False)
            ax.text(x, y - 0.2, f"K{r}{i}" if True else "", ha="center", va="center", fontsize=6, fontfamily=FONT, color="#9ca3af")
            # diode down to the row bus
            diode(ax, a[0], a[1] - 0.8, a[0], a[1] - 8.6)
            blob(ax, a[0], a[1] - 0.3)
            ax.plot([a[0], a[0]], [a[1] - 8.6, y - 10.5], color="#6b7280", lw=1.2)
            blob(ax, a[0], y - 10.5)
            # column wire through pin B
            blob(ax, b[0], b[1] + 0.4)
    for r, y in enumerate((16, -2)):
        ax.plot([-4, 2 * pitch + 6], [y - 10.5, y - 10.5], color=ROW, lw=2.4, zorder=2)
        ax.text(2 * pitch + 7, y - 10.5, f"ROW {r}  ->  Pico", va="center", fontsize=8, fontfamily=FONT, color=ROW,
                weight="bold")
        ax.text(-4.5, y - 10.5, "", va="center")
    for i, x in enumerate(xs):
        bx = x - 2.54
        ax.plot([bx, bx], [-2 + 5.08, 16 + 5.08 + 6], color=COLW, lw=2.4, zorder=2)
        ax.text(bx, 16 + 5.08 + 7.5, f"COL {i}\n-> Pico", ha="center", fontsize=8, fontfamily=FONT, color=COLW, weight="bold")
    ax.text(31, -15.5, "Row wires: the bent cathode legs lap over each other and are soldered together; a piece of wire bridges the gaps.\n"
            "Column wires: one insulated wire per column, stripped at each pin B (or bare wire kept away from the rows).",
            ha="center", fontsize=7.5, fontfamily=FONT, color="#4b5563")
    ax.set_title("Two rows x three columns, seen from below", fontsize=11, fontfamily=FONT, color=INK, loc="left")
    fig.tight_layout()
    for ext in ("png", "svg"):
        fig.savefig(OUT / f"soldering_row.{ext}", dpi=150 if ext == "png" else None, facecolor="white")
    plt.close(fig)
    print("wrote", OUT / "soldering_row.png")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    figure_diode()
    figure_row()
