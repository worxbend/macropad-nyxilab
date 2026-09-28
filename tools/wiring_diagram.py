#!/usr/bin/env python3
"""Generate the wiring diagrams from each edition's firmware/include/config.h:

  docs/images/wiring_joystick.svg|png   joystick edition   (joystick/firmware/include/config.h)
  docs/images/wiring_rotary.svg|png     rotary edition     (rotary/firmware/include/config.h)
  docs/images/matrix.svg|png            key matrix (both editions)

The pin numbers are parsed from the firmware configs, so the diagrams always
match the code; the tool also checks that the shared pins agree between the two
editions.  Run from anywhere:  python3 tools/wiring_diagram.py
"""

from __future__ import annotations

import re
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch, Polygon, Rectangle  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
CONFIGS = {name: REPO / name / "firmware" / "include" / "config.h" for name in ("joystick", "rotary")}
OUT = REPO / "docs" / "images"
FONT = "DejaVu Sans"

# Pico / Pico 2 pinout, physical pin -> name (top view, USB at the top)
PICO = {
    1: "GP0", 2: "GP1", 3: "GND", 4: "GP2", 5: "GP3", 6: "GP4", 7: "GP5", 8: "GND", 9: "GP6", 10: "GP7",
    11: "GP8", 12: "GP9", 13: "GND", 14: "GP10", 15: "GP11", 16: "GP12", 17: "GP13", 18: "GND", 19: "GP14",
    20: "GP15", 21: "GP16", 22: "GP17", 23: "GND", 24: "GP18", 25: "GP19", 26: "GP20", 27: "GP21", 28: "GND",
    29: "GP22", 30: "RUN", 31: "GP26", 32: "GP27", 33: "AGND", 34: "GP28", 35: "ADC_VREF", 36: "3V3",
    37: "3V3_EN", 38: "GND", 39: "VSYS", 40: "VBUS",
}

COL = {
    "row": "#2563eb", "col": "#16a34a", "main": "#7c3aed", "bar": "#0d9488", "joy": "#ea580c",
    "pwr": "#dc2626", "gnd": "#111827", "free": "#9ca3af", "led": "#c026d3",
}


def parse_config(path: Path) -> dict:
    src = Path(path).read_text()
    vals: dict = {}
    for name, body in re.findall(r"constexpr uint8_t (\w+)\[[^\]]*\]\s*=\s*\{([^}]*)\}", src):
        vals[name] = [int(x) for x in re.findall(r"\d+", body)]
    for decl in re.findall(r"constexpr uint8_t ([^;]+);", src):
        for part in decl.split(","):
            m = re.match(r"\s*(\w+)\s*=\s*(\d+)\s*$", part)
            if m:
                vals[m.group(1)] = int(m.group(2))
    return vals


def assignments(v: dict, rotary: bool) -> dict[int, tuple[str, str]]:
    """GPIO -> (function label, colour key)."""
    a: dict[int, tuple[str, str]] = {}
    for i, g in enumerate(v["ROW_PINS"]):
        a[g] = (f"ROW{i} ({['back', '2nd', '3rd', 'front'][i]} row)", "row")
    for i, g in enumerate(v["COL_PINS"]):
        a[g] = (f"COL{i} ({['left column', 'right col A', 'right col B'][i]})", "col")
    for key, lab in (("MAIN_SCK", "SCL"), ("MAIN_MOSI", "SDA"), ("MAIN_CS", "CS"), ("MAIN_DC", "DC"),
                     ("MAIN_RST", "RES"), ("MAIN_BL", "BLK")):
        a[v[key]] = (f"1.9in {lab}", "main")
    for key, lab in (("BAR_SCK", "SCL"), ("BAR_MOSI", "SDA"), ("BAR_CS", "CS"), ("BAR_DC", "DC"),
                     ("BAR_RST", "RST"), ("BAR_BL", "BL")):
        a[v[key]] = (f"2.25in {lab}", "bar")
    if rotary:
        a[v["ENC_A_PIN"]] = ("Encoder CLK", "joy")
        a[v["ENC_B_PIN"]] = ("Encoder DT", "joy")
        a[v["ENC_SW_PIN"]] = ("Encoder SW", "joy")
    else:
        a[v["JOY_X_PIN"]] = ("Joystick VRx", "joy")
        a[v["JOY_Y_PIN"]] = ("Joystick VRy", "joy")
        a[v["JOY_SW_PIN"]] = ("Joystick SW", "joy")
    a[v["LED_PIN"]] = ("LED DIN (330 R in line)", "led")
    return a


def wiring(v: dict, rotary: bool = False):
    a = assignments(v, rotary)
    centre = "encoder" if rotary else "joystick"
    fig = plt.figure(figsize=(15.2, 10.2))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 152)
    ax.set_ylim(0, 102)
    rx = 111  # right-hand column, clear of the pin labels
    ax.axis("off")
    ax.set_aspect("equal")

    # Pico body
    px, py, pw, ph = 52, 18, 26, 70
    ax.add_patch(FancyBboxPatch((px, py), pw, ph, boxstyle="round,pad=0,rounding_size=2", fc="#b5122b", ec="#5a0a15", lw=1.2))
    ax.add_patch(Rectangle((px + pw / 2 - 4.5, py + ph - 1.5), 9, 5, fc="#c9ccd1", ec="#6b7280", lw=0.8))
    ax.text(px + pw / 2, py + ph + 5.5, "USB-C  (back of the case)", ha="center", fontsize=8, fontfamily=FONT)
    ax.text(px + pw / 2, py + ph / 2, "Pico 2\nRP2350\n\ntop view", ha="center", va="center", fontsize=11,
            color="white", fontfamily=FONT, weight="bold")
    pitch = ph / 20.5
    for pin, name in PICO.items():
        left = pin <= 20
        idx = pin - 1 if left else 40 - pin
        y = py + ph - (idx + 0.75) * pitch
        x = px if left else px + pw
        gp = int(name[2:]) if name.startswith("GP") else None
        if gp is not None and gp in a:
            label, ck = a[gp]
        elif name in ("GND", "AGND"):
            label, ck = name, "gnd"
        elif name == "3V3":
            label, ck = f"3V3 OUT -> displays + {centre}", "pwr"
        elif name == "VBUS":
            label, ck = "VBUS 5V -> 1N4001 -> LED 5V", "pwr"
        else:
            label, ck = "", "free"
        ax.add_patch(plt.Circle((x, y), 0.9, fc="#e5c07b", ec="#7a5c1c", lw=0.6))
        ax.text(x + (2.2 if left else -2.2), y, f"{pin}", ha="left" if left else "right", va="center", fontsize=5.8,
                color="#f4b6c0", fontfamily=FONT)
        ax.text(x + (5.2 if left else -5.2), y, name, ha="left" if left else "right", va="center", fontsize=6.3,
                color="white", fontfamily=FONT)
        if label:
            lx = x - 6 if left else x + 6
            ax.plot([x + (-1 if left else 1), lx], [y, y], color=COL[ck], lw=1.4)
            ax.text(lx + (-0.8 if left else 0.8), y, label, ha="right" if left else "left", va="center", fontsize=7.4,
                    color=COL[ck], fontfamily=FONT, weight="bold" if ck not in ("gnd",) else "normal")

    # component header cards
    def card(x, y, title, rows, ck, w=38):
        h = 4 + 3.1 * len(rows)
        ax.add_patch(FancyBboxPatch((x, y - h), w, h, boxstyle="round,pad=0,rounding_size=1.2", fc="#f8fafc",
                                    ec=COL[ck], lw=1.3))
        ax.text(x + 1.5, y - 2.2, title, fontsize=8.2, weight="bold", color=COL[ck], fontfamily=FONT, va="center")
        for i, (pin, to) in enumerate(rows):
            yy = y - 5.2 - i * 3.1
            ax.text(x + 2, yy, pin, fontsize=7.4, fontfamily="DejaVu Sans Mono", va="center")
            ax.text(x + 13, yy, "->  " + to, fontsize=7.4, fontfamily=FONT, va="center")

    g = lambda k: f"GP{v[k]}"  # noqa: E731
    card(2, 99, "1.9in ST7789 170x320 (8-pin)", [("GND", "GND"), ("VCC", "3V3 (pin 36)"), ("SCL", g("MAIN_SCK")),
         ("SDA", g("MAIN_MOSI")), ("RES", g("MAIN_RST")), ("DC", g("MAIN_DC")), ("CS", g("MAIN_CS")),
         ("BLK", g("MAIN_BL") + " (PWM)")], "main")
    card(2, 64, "2.25in ST7789P3 76x284 (8-pin)", [("GND", "GND"), ("VCC", "3V3 (pin 36)"), ("SCL", g("BAR_SCK")),
         ("SDA", g("BAR_MOSI")), ("RST", g("BAR_RST")), ("DC", g("BAR_DC")), ("CS", g("BAR_CS")),
         ("BL", g("BAR_BL") + " (PWM)")], "bar")
    if rotary:
        card(2, 29, "KY-040 rotary encoder (5-pin)", [("GND", "GND (pin 28)"), ("+", "3V3 (pin 36)  NOT 5V!"),
             ("SW", g("ENC_SW_PIN")), ("DT", g("ENC_B_PIN")), ("CLK", g("ENC_A_PIN"))], "joy")
    else:
        card(2, 29, "KY-023 joystick (5-pin)", [("GND", "AGND (pin 33)"), ("+5V", "3V3 (pin 36)  NOT 5V!"),
             ("VRx", g("JOY_X_PIN") + " / ADC0"), ("VRy", g("JOY_Y_PIN") + " / ADC1"), ("SW", g("JOY_SW_PIN"))], "joy")
    card(rx, 99, "Key matrix (see matrix diagram)", [(f"ROW{i}", f"GP{p}") for i, p in enumerate(v["ROW_PINS"])]
         + [(f"COL{i}", f"GP{p}") for i, p in enumerate(v["COL_PINS"])], "row")
    card(rx, 70.5, "2x WS2812B LED stick, chained", [
        ("5V", "VBUS (pin 40) via 1N4001, band -> sticks"),
        ("GND", "GND (pin 38)"),
        ("DIN", g("LED_PIN") + " via 330 R  (right stick, back end)"),
        ("DOUT", "left stick DIN  (both front ends)"),
    ], "led", w=38)
    notes = [
        f"Displays and the {centre} run from 3V3 (pin 36): the display",
        "logic" + (" and the encoder's pull-ups" if rotary else " and the ADC") + " are 3.3 V only.",
        "Only the LED sticks take 5 V: VBUS through the diode (~4.3 V),",
        "so the Pico's 3.3 V data is a valid high for the first LED.",
        "Displays share nothing: SPI0 -> 1.9in, SPI1 -> 2.25in.  Keep SPI",
        "wires short (lower SPI_HZ in config.h if you see glitches).",
        "Free pins: GP0, GP1 (UART), GP9.",
    ]
    for i, n in enumerate(notes):
        ax.text(rx, 48 - i * 3.2, n, fontsize=7.4, fontfamily=FONT, color="#374151")
    ax.text(65, 8, "Generated from firmware/include/config.h", ha="center", fontsize=7, color="#9ca3af",
            fontfamily=FONT)
    ax.text(65, 4.5, "Rotary edition" if rotary else "Joystick edition", ha="center", fontsize=9,
            color="#374151", fontfamily=FONT, weight="bold")
    stem = "wiring_rotary" if rotary else "wiring_joystick"
    for ext in ("svg", "png"):
        fig.savefig(OUT / f"{stem}.{ext}", dpi=150 if ext == "png" else None, facecolor="white")
    plt.close(fig)


def matrix(v: dict):
    rows, cols = v["ROW_PINS"], v["COL_PINS"]
    fig = plt.figure(figsize=(11, 8.2))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 110)
    ax.set_ylim(0, 82)
    ax.axis("off")
    ax.set_aspect("equal")
    x0, y0, dx, dy = 22, 66, 28, 16
    for c, gp in enumerate(cols):
        x = x0 + c * dx + 8
        ax.plot([x, x], [y0 + 6, y0 - 3 * dy - 6], color=COL["col"], lw=2)
        ax.text(x, y0 + 8.5, f"COL{c}\nGP{gp}", ha="center", fontsize=9, color=COL["col"], weight="bold", fontfamily=FONT)
    for r, gp in enumerate(rows):
        y = y0 - r * dy - 7
        ax.plot([x0 - 6, x0 + 2 * dx + 16], [y, y], color=COL["row"], lw=2)
        ax.text(x0 - 8, y, f"ROW{r}  GP{gp}", ha="right", va="center", fontsize=9, color=COL["row"], weight="bold",
                fontfamily=FONT)
        for c in range(len(cols)):
            xc = x0 + c * dx + 8
            yk = y0 - r * dy
            # column tap -> switch -> diode (anode) -> cathode bar -> row wire
            ax.plot([xc, xc - 6], [yk, yk], color="#374151", lw=1.2)
            ax.add_patch(Rectangle((xc - 11, yk - 1.6), 5, 3.2, fc="#f3f4f6", ec="#374151", lw=1.0))
            ax.text(xc - 8.5, yk + 3, f"K{r}{c}", ha="center", fontsize=6.5, fontfamily=FONT, color="#374151")
            ax.plot([xc - 11, xc - 13], [yk, yk], color="#374151", lw=1.2)
            ax.plot([xc - 13, xc - 13], [yk, y + 2.2], color="#374151", lw=1.2)
            ax.add_patch(Polygon([(xc - 14.4, y + 4.6), (xc - 11.6, y + 4.6), (xc - 13, y + 2.2)], closed=True,
                                 fc="#111827"))
            ax.plot([xc - 14.6, xc - 11.4], [y + 2.2, y + 2.2], color="#111827", lw=1.6)
            ax.plot([xc - 13, xc - 13], [y + 2.2, y], color="#374151", lw=1.2)
            ax.add_patch(plt.Circle((xc - 13, y), 0.55, color=COL["row"]))
            ax.add_patch(plt.Circle((xc, yk), 0.55, color=COL["col"]))
    txt = [
        "COL2ROW: every switch sits between its COLUMN wire and a 1N4148 whose cathode (black band) goes to the ROW wire.",
        "Rows run across the whole pad (left column -> under the displays -> right block); columns run front-to-back.",
        "ROW0 is the back row (next to the USB port), ROW3 the front row.  COL0 is the column next to the bar display.",
    ]
    for i, t in enumerate(txt):
        ax.text(4, 8.5 - i * 3, t, fontsize=7.8, fontfamily=FONT, color="#374151")
    for ext in ("svg", "png"):
        fig.savefig(OUT / f"matrix.{ext}", dpi=150 if ext == "png" else None, facecolor="white")
    plt.close(fig)


SHARED = ("ROW_PINS", "COL_PINS", "MAIN_SCK", "MAIN_MOSI", "MAIN_CS", "MAIN_DC", "MAIN_RST", "MAIN_BL", "BAR_SCK",
          "BAR_MOSI", "BAR_CS", "BAR_DC", "BAR_RST", "BAR_BL", "LED_PIN", "LED_COUNT")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    cfgs = {name: parse_config(path) for name, path in CONFIGS.items()}
    for key in SHARED:  # the two editions must wire the shared parts identically (docs/wiring.md has one pin map)
        assert cfgs["joystick"][key] == cfgs["rotary"][key], f"{key} differs between the editions' config.h"
    wiring(cfgs["joystick"])
    wiring(cfgs["rotary"], rotary=True)
    matrix(cfgs["joystick"])
    print("wrote", OUT / "wiring_joystick.svg", OUT / "wiring_rotary.svg", "and", OUT / "matrix.svg")
