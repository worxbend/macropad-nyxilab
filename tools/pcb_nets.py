#!/usr/bin/env python3
"""Nets and connector pinouts of the PCB version: the source of truth for the KiCad boards.

    python tools/pcb_nets.py        -> pcb/netlist.json, pcb/pinout.md

One set of boards serves both editions: the GPIOs are the same (docs/wiring.md), the centre control's
three nets C_A / C_B / C_SW are the encoder's CLK / DT / SW or the joystick's VRx / VRy / SW.  Checks
that every GPIO the firmware's config.h uses reaches the right Pico pin, exactly once, in both editions.
"""

from __future__ import annotations

import json
import re
import sys

from editions import EDITIONS, REPO

from pcb_layout import PICO_GP, POWER

# net -> (config.h name, index into an array or None); the GPIO comes from config.h, not from here
FIRMWARE_NETS = {
    **{f"ROW{i}": ("ROW_PINS", i) for i in range(4)},
    **{f"COL{i}": ("COL_PINS", i) for i in range(3)},
    **{f"MAIN_{s}": (f"MAIN_{s}", None) for s in ("SCK", "MOSI", "CS", "DC", "RST", "BL")},
    **{f"BAR_{s}": (f"BAR_{s}", None) for s in ("SCK", "MOSI", "CS", "DC", "RST", "BL")},
    "LED_DATA": ("LED_PIN", None),
}
CENTRE = {  # net -> config.h name per edition
    "C_A": {"rotary": "ENC_A_PIN", "joystick": "JOY_X_PIN"},
    "C_B": {"rotary": "ENC_B_PIN", "joystick": "JOY_Y_PIN"},
    "C_SW": {"rotary": "ENC_SW_PIN", "joystick": "JOY_SW_PIN"},
}

# Flat cables (20-pin, 0.5 mm FFC), pin 1 .. 20.  GND next to each SPI clock and data line and at both edges; 3V3 on
# pin 2 only (one 0.5 A contact; a cable draws well under 100 mA), which leaves the hats room to route it.
FFC_LEFT = ["GND", "3V3", "ROW0", "ROW1", "ROW2", "ROW3", "COL0", "GND", "BAR_SCK", "GND", "BAR_MOSI", "BAR_DC",
            "BAR_CS", "BAR_RST", "BAR_BL", "GND", "C_A", "C_B", "C_SW", "GND"]
# The main display's lines first: on the hat they come from the Pico's other pin row than the matrix lines, and in
# this order the two groups reach J2 side by side instead of crossing (the router left one line unrouted).
FFC_RIGHT = ["GND", "3V3", "MAIN_BL", "MAIN_RST", "MAIN_CS", "MAIN_DC", "MAIN_MOSI", "GND", "MAIN_SCK", "GND",
             "COL2", "COL1", "ROW3", "ROW2", "ROW1", "ROW0", "GND", "NC", "NC", "GND"]
# Pigtails, pin 1 first.  The displays' order is their module header's (docs/wiring.md, "Display modules").
DISPLAY_SH8 = {"main": ["GND", "3V3", "MAIN_SCK", "MAIN_MOSI", "MAIN_RST", "MAIN_DC", "MAIN_CS", "MAIN_BL"],
               "bar": ["GND", "3V3", "BAR_SCK", "BAR_MOSI", "BAR_RST", "BAR_DC", "BAR_CS", "BAR_BL"]}
ENCODER_SH5 = ["C_A", "C_B", "C_SW", "3V3", "GND"]  # KY-040 header order: CLK DT SW + GND
JOYSTICK_SH5 = ["GND", "3V3", "C_A", "C_B", "C_SW"]  # KY-023 header order: GND +5V VRx VRy SW (powered from 3V3)
LED_PH3 = ["LED_5V", "LED_DIN", "GND"]  # to the right stick's DIN end

BOARDS = {
    "left": {
        "parts": {
            "J1": ("FPC 20-pin 0.5 mm, to the hat", FFC_LEFT),
            "J2": ("JST-SH 8, bar display pigtail", DISPLAY_SH8["bar"]),
            "J3": ("JST-SH 5, encoder (rotary edition; not fitted on the joystick edition)", ENCODER_SH5),
            "C1": ("100 nF 0402, 3V3 at J1", ["3V3", "GND"]),
        },
        "switches": [(r, 0) for r in range(4)],
    },
    "right": {
        "parts": {
            "J1": ("FPC 20-pin 0.5 mm, to the hat", FFC_RIGHT),
            "J2": ("JST-SH 8, main display pigtail", DISPLAY_SH8["main"]),
            "C1": ("100 nF 0402, 3V3 at J1", ["3V3", "GND"]),
        },
        "switches": [(r, c) for r in range(4) for c in (1, 2)],
    },
    "hat": {
        "parts": {
            "J1": ("FPC 20-pin 0.5 mm, from the left board", FFC_LEFT),
            "J2": ("FPC 20-pin 0.5 mm, from the right board", FFC_RIGHT),
            "J3": ("JST-SH 5, joystick (joystick edition; not fitted on the rotary edition)", JOYSTICK_SH5),
            "J4": ("JST-PH 3, LED sticks", LED_PH3),
            "D1": ("Schottky 1 A SOD-123 (B5819W class), VBUS -> LED_5V", ["VBUS", "LED_5V"]),
            "U1": ("74AHCT1G125 SOT-23-5: 3.3 V data -> 5 V data", {"1 OE": "GND", "2 A": "LED_DATA", "3 GND": "GND",
                                                                    "4 Y": "LED_Y", "5 VCC": "LED_5V"}),
            "R1": ("330 R 0402, LED data series resistor", ["LED_Y", "LED_DIN"]),
            "C1": ("10 uF 0805, LED_5V bulk", ["LED_5V", "GND"]),
            "C2": ("100 nF 0402, U1 supply", ["LED_5V", "GND"]),
            "C3": ("100 nF 0402, 3V3 at J1", ["3V3", "GND"]),
            "C4": ("100 nF 0402, 3V3 at J2", ["3V3", "GND"]),
            "H1": ("2 x 20 pin header 2.54 mm to the Pico 2's pin rows (the Pico's own pinout)", "pico"),
        },
    },
}


def config_pins(edition: str) -> dict[str, list[int]]:
    text = (REPO / edition / "firmware" / "include" / "config.h").read_text()
    out = {}
    for name, body in re.findall(r"constexpr uint8_t (\w+)\[[^]]*\]\s*=\s*\{([^}]*)\}", text):
        out[name] = [int(v) for v in body.split(",")]
    for name, value in re.findall(r"(\w+) = (\d+)[,;]", text):
        out.setdefault(name, [int(value)])
    return out


def gpio_nets(edition: str) -> dict[int, str]:
    """GPIO -> net in this edition, from config.h."""
    cfg = config_pins(edition)
    nets = {}
    for net, (key, i) in FIRMWARE_NETS.items():
        nets[cfg[key][i or 0]] = net
    for net, keys in CENTRE.items():
        nets[cfg[keys[edition]][0]] = net
    return nets


def pico_nets(edition: str) -> dict[int, str]:
    """Physical Pico pin -> net."""
    by_gpio = gpio_nets(edition)
    out = {}
    for pin in range(1, 41):
        if pin in PICO_GP:
            out[pin] = by_gpio.get(PICO_GP[pin], "NC")
        else:
            out[pin] = {"3V3": "3V3", "VBUS": "VBUS", "GND": "GND", "AGND": "GND"}.get(POWER.get(pin, ""), "NC")
    return out


def check() -> None:
    per_edition = {e: pico_nets(e) for e in EDITIONS}
    if per_edition["joystick"] != per_edition["rotary"]:
        diff = {p: (per_edition["joystick"][p], per_edition["rotary"][p]) for p in range(1, 41)
                if per_edition["joystick"][p] != per_edition["rotary"][p]}
        raise SystemExit(f"the editions put different nets on Pico pins {diff}: one hat cannot serve both")
    for e in EDITIONS:
        used = gpio_nets(e)
        if len(used) != len(FIRMWARE_NETS) + len(CENTRE):
            raise SystemExit(f"{e}: two nets share a GPIO in config.h")
        missing = [g for g in used if g not in PICO_GP.values()]
        if missing:
            raise SystemExit(f"{e}: GPIOs {missing} are not on the Pico's pin rows")
    signals = set(FIRMWARE_NETS) | set(CENTRE)
    carried = {n for n in FFC_LEFT + FFC_RIGHT + JOYSTICK_SH5 + ["LED_DATA"]}
    if signals - carried:
        raise SystemExit(f"nets that reach no board: {sorted(signals - carried)}")
    for name, cable in (("left", FFC_LEFT), ("right", FFC_RIGHT)):
        if len(cable) != 20:
            raise SystemExit(f"FFC {name} has {len(cable)} pins")
        for i, net in enumerate(cable):  # every SPI clock and data line between grounds (or a ground and power)
            if net.endswith(("SCK", "MOSI")) and "GND" not in (cable[i - 1], cable[i + 1]):
                raise SystemExit(f"FFC {name}: {net} has no ground beside it")
    for board in ("left", "right"):
        on_ffc = set(BOARDS[board]["parts"]["J1"][1])
        local = {n for _, (_, nets) in BOARDS[board]["parts"].items() if isinstance(nets, list) for n in nets}
        stray = local - on_ffc - {"NC"}
        if stray:
            raise SystemExit(f"{board} board uses nets its flat cable does not carry: {sorted(stray)}")


def write() -> None:
    nets = pico_nets("rotary")
    out = REPO / "pcb"
    out.mkdir(exist_ok=True)
    data = {"pico_pins": {str(p): n for p, n in nets.items()}, "boards": {}}
    for b, spec in BOARDS.items():
        data["boards"][b] = {ref: {"part": desc, "pins": pins if pins != "pico" else data["pico_pins"]}
                             for ref, (desc, pins) in spec["parts"].items()}
        if "switches" in spec:
            data["boards"][b]["switches"] = [{"ref": f"SW_R{r}C{c}", "diode": f"D_R{r}C{c}", "col": f"COL{c}",
                                              "row": f"ROW{r}"} for r, c in spec["switches"]]
    (out / "netlist.json").write_text(json.dumps(data, indent=2) + "\n")

    def table(title, pins):
        rows = [f"### {title}", "", "| pin | net |", "|---|---|"]
        rows += [f"| {i} | {n} |" for i, n in enumerate(pins, 1)]
        return "\n".join(rows) + "\n"

    gp = {n: g for g, n in gpio_nets("rotary").items()}
    centre = {n: f"{v['rotary'].removesuffix('_PIN')} / {v['joystick'].removesuffix('_PIN')}" for n, v in CENTRE.items()}
    md = ["# PCB version: nets and pinouts", "",
          "Generated by `tools/pcb_nets.py` from `config.h` of both editions; edit that file, not this one.", "",
          "## Nets on the Pico 2", "", "| net | GPIO | Pico pin | firmware name |", "|---|---|---|---|"]
    for net in list(FIRMWARE_NETS) + list(CENTRE):
        pin = next(p for p, n in nets.items() if n == net)
        md.append(f"| {net} | GP{gp[net]} | {pin} | {centre.get(net, FIRMWARE_NETS.get(net, ('',))[0])} |")
    md += ["", "Power: 3V3 = pin 36 (3V3 OUT), VBUS = pin 40, GND = pins 3, 8, 13, 18, 23, 28, 33 (AGND), 38.", "",
           "## Cables", "",
           "FFC pin 1 is pin 1 at both ends. Whether that needs a same-side (type A) or opposite-side (type B) cable "
           "depends on the contact sides of the two FPC connectors: settle it from their datasheets in KiCad.", "",
           table("FFC left board <-> hat (20-pin 0.5 mm)", FFC_LEFT),
           table("FFC right board <-> hat (20-pin 0.5 mm)", FFC_RIGHT),
           table("Main display pigtail (JST-SH 8, pin 1 = the module's GND)", DISPLAY_SH8["main"]),
           table("Bar display pigtail (JST-SH 8, pin 1 = the module's GND)", DISPLAY_SH8["bar"]),
           table("Encoder KY-040 (JST-SH 5 on the left board)", ENCODER_SH5),
           table("Joystick KY-023 (JST-SH 5 on the hat)", JOYSTICK_SH5),
           table("LED sticks (JST-PH 3 on the hat, to the right stick's DIN end)", LED_PH3),
           "## Parts per board", ""]
    for b, spec in BOARDS.items():
        md.append(f"### {b}")
        md.append("")
        for ref, (desc, _) in spec["parts"].items():
            md.append(f"- {ref}: {desc}")
        if "switches" in spec:
            md.append(f"- {len(spec['switches'])} MX switches (soldered, or Mill-Max 0305 sockets) and as many "
                      "1N4148W diodes (SOD-123): column -> switch -> diode anode, cathode -> row (COL2ROW)")
        md.append("")
    (out / "pinout.md").write_text("\n".join(md))


def main() -> int:
    check()
    write()
    print("nets checked for", ", ".join(EDITIONS), "; wrote pcb/netlist.json, pcb/pinout.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
