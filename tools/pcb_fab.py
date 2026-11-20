#!/usr/bin/python3
"""Order files for the PCB version: Gerbers, drill, pick-and-place and a BOM per board, for PCBWay's PCBA form.

    /usr/bin/python3 tools/pcb_fab.py        -> pcb/fab/<board>/   (run make pcb-kicad first)

Per board: <board>-gerbers.zip (copper, mask, paste, silkscreen, outline, drill), <board>-pos.csv (both sides, mm)
and <board>-bom.csv; for docs/pcb-build.md the KiCad 3D renders and the fit check's previews in docs/images/pcb/
(the previews need rsvg-convert).  The BOM marks what the assembler fits, what you fit yourself (the MX switches) and what
belongs to one edition only.  Part numbers are the parts the boards were laid out for (docs/pcb-build.md says
where stock was unconfirmed); generic passives leave the maker to the assembler.
"""

from __future__ import annotations

import csv
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import pcbnew

REPO = Path(__file__).resolve().parents[1]
KICAD = REPO / "pcb" / "kicad"
FAB = REPO / "pcb" / "fab"
IMAGES = REPO / "docs" / "images" / "pcb"
BOARDS = ("left", "right", "hat-rotary", "hat-joystick")
LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts"

# footprint -> (manufacturer, part number, description)
PARTS = {
    "Hirose_FH12-20S-0.5SH_1x20-1MP_P0.50mm_Horizontal":
        ("Hirose", "FH12-20S-0.5SH(55)", "FPC connector, 20 pins, 0.5 mm, bottom contact, ZIF"),
    "JST_SH_SM08B-SRSS-TB_1x08-1MP_P1.00mm_Horizontal":
        ("JST", "SM08B-SRSS-TB(LF)(SN)", "JST-SH header, 8 pins, 1.0 mm, side entry"),
    "JST_SH_SM05B-SRSS-TB_1x05-1MP_P1.00mm_Horizontal":
        ("JST", "SM05B-SRSS-TB(LF)(SN)", "JST-SH header, 5 pins, 1.0 mm, side entry"),
    "JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal":
        ("JST", "S3B-PH-SM4-TB(LF)(SN)", "JST-PH header, 3 pins, 2.0 mm, side entry, SMD"),
    "SOT-23-5": ("Texas Instruments", "SN74AHCT1G125DBVR", "single buffer, 3-state, 5 V TTL-level inputs"),
    "SW_Cherry_MX_1.00u_Plate": ("", "", "MX switch, plate-mounted"),
    "RaspberryPi_Pico_Common_THT": ("", "", "2 x 1x20 pin header, 2.54 mm, straight, to the Pico 2"),
}
BY_VALUE = {  # (footprint, value) -> (manufacturer, part number, description)
    ("D_SOD-123", "1N4148W"): ("any (e.g. Changjiang CJ)", "1N4148W", "switching diode, 75 V, SOD-123"),
    ("D_SOD-123", "B5819W"): ("any in stock (e.g. MDD)", "B5819W", "Schottky diode, 40 V 1 A, SOD-123"),
    ("C_0402_1005Metric", "100nF"): ("any", "", "capacitor 100 nF, X7R, 16 V, 0402"),
    ("C_0805_2012Metric", "10uF"): ("any", "", "capacitor 10 uF, X5R, 10 V, 0805"),
    ("R_0402_1005Metric", "330R"): ("any", "", "resistor 330 R, 1 %, 0402"),
}


def note(board: str, ref: str, footprint: str) -> str:
    if footprint.startswith("SW_Cherry_MX"):
        return "do not assemble: you fit your own switches through the top plate"
    if footprint.startswith("RaspberryPi_Pico"):
        return "through-hole: assemble the two 1x20 headers into the hat (long pins down); the Pico itself is yours"
    if board == "left" and ref == "J3":
        return "rotary edition only (encoder); leave out on the joystick edition"
    if board == "hat-joystick" and ref == "J3":
        return "joystick edition only"
    return ""


def bom(board: str, b: pcbnew.BOARD) -> list[dict]:
    groups: dict[tuple, dict] = {}
    for fp in b.GetFootprints():
        name = fp.GetFPID().GetLibItemName().wx_str()
        value = fp.GetValue()
        maker, mpn, desc = PARTS.get(name) or BY_VALUE.get((name, value)) or ("", "", value)
        key = (name, value, note(board, fp.GetReference(), name))
        g = groups.setdefault(key, {"refs": [], "maker": maker, "mpn": mpn, "desc": desc, "footprint": name,
                                    "type": "THT" if any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for p in fp.Pads())
                                    else "SMD", "side": "bottom" if fp.IsFlipped() else "top", "note": key[2]})
        g["refs"].append(fp.GetReference())
    rows = []
    for i, g in enumerate(sorted(groups.values(), key=lambda g: (g["note"].startswith("do not"), g["refs"][0])), 1):
        refs = sorted(g["refs"], key=lambda r: (r.rstrip("0123456789"), int("".join(c for c in r if c.isdigit()) or 0)))
        rows.append({"Item": i, "Designator": ",".join(refs), "Qty": len(refs), "Manufacturer": g["maker"],
                     "Mfg Part #": g["mpn"], "Description": g["desc"], "Package / Footprint": g["footprint"],
                     "Type": g["type"], "Side": g["side"], "Note": g["note"]})
    return rows


def export(board: str) -> list[dict]:
    pcb = KICAD / board / f"{board}.kicad_pcb"
    out = FAB / board
    shutil.rmtree(out, ignore_errors=True)
    gerbers = out / "gerbers"
    gerbers.mkdir(parents=True)
    run = lambda *args: subprocess.run(["kicad-cli", "pcb", "export", *args], check=True,  # noqa: E731
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run("gerbers", "--layers", LAYERS, "--subtract-soldermask", "--check-zones", "-o", str(gerbers) + "/", str(pcb))
    run("drill", "--format", "excellon", "--excellon-separate-th", "--generate-map", "--map-format", "pdf",
        "-o", str(gerbers) + "/", str(pcb))
    run("pos", "--format", "csv", "--units", "mm", "--side", "both", "-o", str(out / f"{board}-pos.csv"), str(pcb))
    with zipfile.ZipFile(out / f"{board}-gerbers.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(gerbers.iterdir()):
            z.write(f, f.name)
    shutil.rmtree(gerbers)
    rows = bom(board, pcbnew.LoadBoard(str(pcb)))
    with (out / f"{board}-bom.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    return rows


def images() -> None:
    IMAGES.mkdir(parents=True, exist_ok=True)
    for board in BOARDS:
        sides = ("top", "bottom") if board in ("left", "right") else ("top",)  # switch boards carry their parts below
        for side in sides:
            subprocess.run(["kicad-cli", "pcb", "render", "--side", side, "--width", "1200", "--height", "1200",
                            "--quality", "basic", "--background", "opaque", "-o", str(IMAGES / f"{board}-{side}.png"),
                            str(KICAD / board / f"{board}.kicad_pcb")],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not shutil.which("rsvg-convert"):
        print("rsvg-convert not found: the fit previews were not converted")
        return
    for edition in ("rotary", "joystick"):
        for preview in ("plate_preview", "hat_preview"):
            subprocess.run(["rsvg-convert", "-w", "1400", "-b", "white", "-o",
                            str(IMAGES / f"{edition}-{preview.replace('_', '-')}.png"),
                            str(REPO / "pcb" / edition / f"{preview}.svg")], check=True)


def main() -> int:
    missing = [b for b in BOARDS if not (KICAD / b / f"{b}.kicad_pcb").exists()]
    if missing:
        raise SystemExit(f"no KiCad board for {missing}: run make pcb-kicad first")
    for board in BOARDS:
        rows = export(board)
        fitted = sum(r["Qty"] for r in rows if not r["Note"].startswith("do not"))
        print(f"{board:13s} {len(rows)} BOM lines, {fitted} parts for the assembler")
    images()
    print("wrote", FAB, "and", IMAGES)
    return 0


if __name__ == "__main__":
    sys.exit(main())
