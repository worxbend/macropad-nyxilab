"""Command line of an edition's ``build.py``.

    python build.py check      fit / interference checks (non-zero exit on failure)
    python build.py export     STEP, STL, 3MF, GLB (+ FreeCAD if FREECAD_CMD is set)
    python build.py render     PNG renders into docs/images/<edition> (needs a GPU/EGL context)
    python build.py drawings   blueprint PDF/SVG + 1:1 template
    python build.py all        everything above, in that order

Outputs go to ``<project>/exports`` and ``<repo>/docs/images/<edition>``.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from . import checks
from .edition import Edition
from .layout import Layout


def cmd_check(L: Layout, exports: Path) -> int:
    res = checks.run(L)
    clear = checks.clearance_report(L)
    for k, v in clear.items():
        print(f"  {k:<36} {v:7.2f} mm")
    print("  printability (overhangs >50 deg, bridges):")
    over = checks.overhang_report(L)
    for name, rep in over.items():
        print(f"    {name:10s} {json.dumps(rep)}")
    # summary for the docs and the 3D viewer
    exports.mkdir(parents=True, exist_ok=True)
    summary = {"edition": L.ed.name, "pairs": len(res), "failing": sum(not r.ok for r in res),
               "clearances_mm": {k: round(v, 2) for k, v in clear.items()}, "printability": over,
               "viewer": {"centre_label": L.ed.centre_label, **L.ed.viewer_info(L, clear)}}
    (exports / "checks.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 0 if all(r.ok for r in res) else 1


def cmd_export(L: Layout, exports: Path, freecad: bool = True) -> int:
    from .export import export_all

    export_all(exports, L, freecad=freecad)
    return 0


def cmd_render(L: Layout, images: Path) -> int:
    from .renders import render_all

    render_all(images, L)
    return 0


def cmd_drawings(L: Layout, exports: Path, images: Path) -> int:
    from .drawings import build_all

    hero = images / "hero.png"
    images.mkdir(parents=True, exist_ok=True)
    pdf = build_all(exports / "drawings", L, render_png=hero if hero.exists() else None,
                    section_to=images / "section_aa.png")
    print("  wrote", pdf)
    return 0


def main(edition: Edition, project_dir: Path, argv=None) -> int:
    """Entry point for ``<edition>/cad/build.py``."""
    project_dir = Path(project_dir).resolve()
    repo = project_dir.parents[1]
    exports = project_dir / "exports"
    images = repo / "docs" / "images" / edition.name
    ap = argparse.ArgumentParser(prog=f"{edition.name}/cad/build.py",
                                 description=f"Build the {edition.name} edition of the case.")
    ap.add_argument("command", choices=["check", "export", "render", "drawings", "all"])
    ap.add_argument("--no-freecad", action="store_true", help="skip the FreeCAD document")
    a = ap.parse_args(argv)
    L = Layout(edition)
    t = time.time()
    steps = {
        "check": [lambda: cmd_check(L, exports)],
        "export": [lambda: cmd_export(L, exports, not a.no_freecad)],
        "render": [lambda: cmd_render(L, images)],
        "drawings": [lambda: cmd_drawings(L, exports, images)],
    }
    steps["all"] = steps["check"] + steps["export"] + steps["render"] + steps["drawings"]
    print(f"== {edition.name} edition ==")
    for step in steps[a.command]:
        rc = step()
        if rc:
            print("FAILED")
            return rc
    print(f"done in {time.time() - t:.0f} s")
    return 0
