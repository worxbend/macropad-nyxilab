"""Colours used for renders, the 3MF/GLB/STEP exports and the FreeCAD file.

Print the top plate and base in colour A and the frame in colour B to get
the two-tone "stripe" from the sketch.  Change freely - only renders use this.
Editions add their own entries (``Edition.part_colors`` etc.).
"""

from __future__ import annotations

from .edition import Edition

PART_COLORS = {
    "top_plate": "#ECEAE4",  # colour A (warm white)
    "frame": "#5B3E8E",  # colour B (night violet)
    "base": "#E3E0D9",  # colour A / C
    "bar_spine": "#8E949A",
}

COMPONENT_COLORS = {
    "cap": "#2B2D33",
    "housing": "#F2F2F0",
    "stem": "#D8436E",
    "pins": "#C9A227",
    "pcb": "#1F5FA8",
    "usb": "#B8BCC0",
    "chips": "#1A1A1A",
    "lcm": "#0D0E11",
    "screen": "#18284A",
    "back": "#2E2E2E",
    "mech": "#A0A0A0",
    "header": "#202020",
    "led": "#F1F0EC",
    "lens": "#CDBDFF",  # lit in the MEDIA layer's violet
    "caps": "#A88A5C",
}

COMPONENT_OVERRIDES = {
    ("pico", "pcb"): "#B5122B",
    ("led", "pcb"): "#111111",
}


def part_color(ed: Edition, name: str) -> str:
    return {**PART_COLORS, **ed.part_colors}[name]


def component_color(ed: Edition, component: str, piece: str) -> str:
    overrides = {**COMPONENT_OVERRIDES, **ed.component_overrides}
    colors = {**COMPONENT_COLORS, **ed.component_colors}
    return overrides.get((component, piece), colors.get(piece, "#777777"))
