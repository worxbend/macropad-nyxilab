"""Write every deliverable into ``cad/exports``.

step/     editable B-rep solids (assembled position) + full colour assembly
stl/      print-ready meshes, already oriented on the bed
3mf/      print-ready, named and coloured (open directly in Bambu/Prusa/Orca)
gltf/     coloured assembly for the web viewer
freecad/  native FreeCAD document (needs FreeCAD; see ``freecad_doc.py``)
"""

from __future__ import annotations

import copy
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from build123d import Color, Compound, Mesher, Pos, export_gltf, export_step, export_stl

from . import components, fit_tests, parts
from .layout import L as DEFAULT_LAYOUT
from .layout import Layout
from .render import hex_rgb
from .style import PART_COLORS, component_color

CAD_DIR = Path(__file__).resolve().parents[1]
EXPORTS = CAD_DIR / "exports"
PLA_DENSITY = 1.24  # g/cm3

PRINT_NOTES = {
    "top_plate": "Print top face DOWN (as exported). 0.2 mm layers, 4 walls, 20% gyroid. Heat-set 8x M3 inserts into the boss ends.",
    "frame": "Print as exported (flat bottom down). 0.2 mm layers, 3 walls, 15% infill. This is the accent colour.",
    "base": "Print as exported. 0.2 mm layers, 3 walls, 20% infill.",
    "bar_spine": "Print as exported (flat side down). 100% infill or 5 walls - it is a small spring clamp.",
}


def _colored(shape, name: str, hex_color: str):
    s = copy.copy(shape)
    s.label = name
    s.color = Color(*hex_rgb(hex_color))
    return s


def assembly(L: Layout = DEFAULT_LAYOUT, with_components: bool = True) -> Compound:
    printed = [_colored(p, n, PART_COLORS[n]) for n, p in parts.all_parts(L).items()]
    children = [Compound(label="printed_parts", children=printed)]
    if with_components:
        groups = []
        for cname, pieces in components.placed_components(L).items():
            kids = [_colored(s, f"{cname} {pn}", component_color(cname.split(" ")[0], pn)) for pn, s in pieces.items()]
            groups.append(Compound(label=cname, children=kids))
        children.append(Compound(label="components", children=groups))
    return Compound(label="nyxilab_macropad", children=children)


def assembly_flat(L: Layout = DEFAULT_LAYOUT, with_components: bool = True) -> Compound:
    """Same content as ``assembly`` but one level deep: the glTF writer drops deeper nesting.
    Leaf labels keep the grouping ("switch K00 housing", "joystick knob", ...)."""
    asm = assembly(L, with_components)
    leaves = list(asm.children[0].children)
    if with_components:
        for group in asm.children[1].children:
            leaves += list(group.children)
    return Compound(label="nyxilab_macropad", children=leaves)


def _write_3mf(path: Path, items: list[tuple[str, object, str]]):
    mesher = Mesher()
    for name, shape, color in items:
        mesher.add_shape(_colored(shape, name, color), linear_deflection=0.01, angular_deflection=0.08)
    mesher.write(str(path))


def export_all(out: Path = EXPORTS, L: Layout = DEFAULT_LAYOUT, freecad: bool = True, verbose: bool = True) -> dict:
    out = Path(out)
    for sub in ("step", "stl", "stl/fit-tests", "3mf", "3mf/fit-tests", "gltf", "freecad"):
        (out / sub).mkdir(parents=True, exist_ok=True)
    manifest: dict = {"layout": L.summary(), "parts": {}, "fit_tests": {}}

    world = parts.all_parts(L)
    for name, part in world.items():
        export_step(_colored(part, name, PART_COLORS[name]), str(out / "step" / f"{name}.step"))
        posed = parts.print_pose(name, part, L)
        export_stl(posed, str(out / "stl" / f"{name}.stl"), tolerance=0.01, angular_tolerance=0.08)
        _write_3mf(out / "3mf" / f"{name}.3mf", [(name, posed, PART_COLORS[name])])
        bb = posed.bounding_box()
        manifest["parts"][name] = {
            "bed_footprint_mm": [round(bb.size.X, 1), round(bb.size.Y, 1)],
            "height_mm": round(bb.size.Z, 1),
            "volume_cm3": round(part.volume / 1000, 1),
            "solid_mass_g_pla": round(part.volume / 1000 * PLA_DENSITY, 0),
            "notes": PRINT_NOTES[name],
        }
        if verbose:
            print(f"  {name:10s} {bb.size.X:6.1f} x {bb.size.Y:5.1f} x {bb.size.Z:5.1f} mm  {part.volume / 1000:6.1f} cm3")

    for name, fn in fit_tests.ALL.items():
        coupon = fn(L)
        bb = coupon.bounding_box()
        coupon = Pos(0, 0, -bb.min.Z) * coupon
        export_stl(coupon, str(out / "stl" / "fit-tests" / f"{name}.stl"), tolerance=0.01, angular_tolerance=0.08)
        _write_3mf(out / "3mf" / "fit-tests" / f"{name}.3mf", [(name, coupon, "#E8B64C")])
        manifest["fit_tests"][name] = [round(bb.size.X, 1), round(bb.size.Y, 1), round(bb.size.Z, 1)]

    asm = assembly(L)
    export_step(asm, str(out / "step" / "macropad_assembly.step"))
    comps_only = Compound(label="reference_components", children=[c for c in asm.children if c.label == "components"])
    export_step(comps_only, str(out / "step" / "reference_components.step"))
    export_gltf(assembly_flat(L), str(out / "gltf" / "macropad_assembly.glb"), binary=True, linear_deflection=0.05,
                angular_deflection=0.3)
    export_gltf(assembly_flat(L, with_components=False), str(out / "gltf" / "macropad_case.glb"), binary=True,
                linear_deflection=0.03, angular_deflection=0.2)

    if freecad:
        fc = export_freecad(out, L, verbose=verbose)
        manifest["freecad"] = os.path.relpath(fc, CAD_DIR) if os.path.isabs(fc) else fc
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def find_freecad() -> str | None:
    cand = [os.environ.get("FREECAD_CMD"), shutil.which("freecadcmd"), shutil.which("FreeCADCmd")]
    return next((c for c in cand if c and Path(c).exists()), None)


def export_freecad(out: Path = EXPORTS, L: Layout = DEFAULT_LAYOUT, verbose: bool = True) -> str:
    fc = find_freecad()
    if not fc:
        msg = "skipped (set FREECAD_CMD to freecadcmd to build the .FCStd)"
        if verbose:
            print("  FreeCAD:", msg)
        return msg
    job = {
        "out": str((out / "freecad" / "nyxilab_macropad.FCStd").resolve()),
        "parts": [{"name": n, "step": str((out / "step" / f"{n}.step").resolve()), "color": hex_rgb(PART_COLORS[n])}
                  for n in parts.all_parts(L)],
        "components_step": str((out / "step" / "reference_components.step").resolve()),
        "params": {k: v for k, v in L.summary().items() if isinstance(v, (int, float))},
    }
    job_file = out / "freecad" / "_job.json"
    job_file.write_text(json.dumps(job))
    script = Path(__file__).with_name("freecad_doc.py")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", MACROPAD_FC_JOB=str(job_file.resolve()))
    Path(job["out"]).unlink(missing_ok=True)
    # FreeCAD's GUI layer captures print(), so success = a freshly written document
    proc = subprocess.run([fc, str(script)], env=env, capture_output=True, text=True, timeout=900)
    job_file.unlink(missing_ok=True)
    ok = proc.returncode == 0 and Path(job["out"]).exists()
    if verbose:
        print("  FreeCAD:", "wrote " + job["out"] if ok else "FAILED\n" + proc.stdout[-2000:] + proc.stderr[-2000:])
    return job["out"] if ok else "failed"


if __name__ == "__main__":
    export_all(freecad="--no-freecad" not in sys.argv)
