"""Command line entry point.

    python -m macropad_cad check      fit / interference checks (non-zero exit on failure)
    python -m macropad_cad export     STEP, STL, 3MF, GLB (+ FreeCAD if FREECAD_CMD is set)
    python -m macropad_cad render     PNG renders into docs/images (needs a GPU/EGL context)
    python -m macropad_cad drawings   blueprint PDF/SVG + 1:1 template
    python -m macropad_cad all        everything above, in that order
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from . import checks
from .export import EXPORTS

REPO = Path(__file__).resolve().parents[2]
DOC_IMAGES = REPO / "docs" / "images"


def cmd_check() -> int:
    res = checks.run()
    for k, v in checks.clearance_report().items():
        print(f"  {k:<36} {v:7.2f} mm")
    print("  printability (overhangs >50 deg, bridges):")
    for name, rep in checks.overhang_report().items():
        print(f"    {name:10s} {json.dumps(rep)}")
    return 0 if all(r.ok for r in res) else 1


def cmd_export(freecad: bool = True) -> int:
    from .export import export_all

    export_all(EXPORTS, freecad=freecad)
    return 0


def cmd_render() -> int:
    from .renders import render_all

    render_all(DOC_IMAGES)
    return 0


def cmd_drawings() -> int:
    from .drawings import build_all

    hero = DOC_IMAGES / "hero.png"
    pdf = build_all(EXPORTS / "drawings", render_png=hero if hero.exists() else None,
                    section_to=DOC_IMAGES / "section_aa.png")
    print("  wrote", pdf)
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="macropad_cad")
    ap.add_argument("command", choices=["check", "export", "render", "drawings", "all"])
    ap.add_argument("--no-freecad", action="store_true", help="skip the FreeCAD document")
    a = ap.parse_args(argv)
    t = time.time()
    steps = {
        "check": [cmd_check],
        "export": [lambda: cmd_export(not a.no_freecad)],
        "render": [cmd_render],
        "drawings": [cmd_drawings],
        "all": [cmd_check, lambda: cmd_export(not a.no_freecad), cmd_render, cmd_drawings],
    }[a.command]
    for step in steps:
        rc = step()
        if rc:
            print("FAILED")
            return rc
    print(f"done in {time.time() - t:.0f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
