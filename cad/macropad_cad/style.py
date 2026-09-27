"""Colours used for renders, the 3MF/GLB/STEP exports and the FreeCAD file.

Print the top plate and base in colour A and the frame in colour B to get
the two-tone "stripe" from the sketch.  Change freely - only renders use this.
"""

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
    "knob": "#1C1C1E",
}

COMPONENT_OVERRIDES = {
    ("pico", "pcb"): "#B5122B",
    ("joystick", "pcb"): "#151515",
}


def component_color(component: str, piece: str) -> str:
    return COMPONENT_OVERRIDES.get((component, piece), COMPONENT_COLORS.get(piece, "#777777"))
