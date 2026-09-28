#!/usr/bin/env python3
"""Embed both editions' GLB models into docs/viewer.html (one self-contained page).

    python3 tools/build_viewer.py

Reads <edition>/cad/exports/gltf/macropad_assembly.glb plus the manifest and the
fit-check summary that `make cad` writes next to them.  Open docs/viewer.html#rotary
to start on the rotary edition.  The website (tools/build_site.py) copies the page.
"""
import base64
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
EDITIONS = ("joystick", "rotary")


def edition(name: str) -> dict:
    out = REPO / name / "cad" / "exports"
    lay = json.loads((out / "manifest.json").read_text())["layout"]
    chk = json.loads((out / "checks.json").read_text())
    info = chk.get("viewer", {})
    plate, wall, base = lay["plate / wall / base"]
    height = f"{lay['front height']:.1f} → {lay['back height']:.1f} mm, {lay['tilt_deg']:.0f}° slope" + info.get("height_extra", "")
    facts = [
        ["Footprint", f"{lay['W']:.1f} × {lay['D']:.1f} mm"],
        ["Height", height],
        ["Layers", f"plate {plate:.1f} · wall {wall:.1f} · base {base:.1f}"],
        ["Keys", "12 × MX, 19.05 pitch"],
        ["Centre", info.get("centre", "")],
        ["LEDs", "2 × 8 WS2812B on the base"],
        ["Fasteners", "8 × M3×8 csk → inserts"],
        ["USB-C", f"centred, {lay['usb center'][2]:.1f} mm above desk"],
        ["Fit checks", f"{chk['pairs']} pairs · {chk['failing']} clashes", "ok" if chk["failing"] == 0 else ""],
    ]
    files = [f"{name}/cad/exports/3mf/ — print-ready parts", f"{name}/cad/exports/step/ — editable solids",
             f"{name}/cad/exports/freecad/nyxilab_macropad_{name}.FCStd",
             f"{name}/cad/exports/drawings/nyxilab_macropad_{name}_blueprints.pdf",
             f"{name}/firmware/dist/nyxilab-macropad-{name}-pico2.uf2"]
    glb = (out / "gltf" / "macropad_assembly.glb").read_bytes()
    return {"glb": base64.b64encode(glb).decode("ascii"), "facts": facts, "files": files,
            "section_note": info.get("section_note", ""), "centre_label": info.get("centre_label", "Centre")}


def main():
    tpl = (REPO / "tools" / "viewer_template.html").read_text()
    data = {name: edition(name) for name in EDITIONS}
    page = tpl.replace("__EDITIONS__", json.dumps(data, ensure_ascii=False))
    out = REPO / "docs" / "viewer.html"
    # standalone copy: add the document skeleton the artifact host would otherwise provide
    out.write_text('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
                   '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
                   '</head>\n<body>\n' + page + '\n</body>\n</html>\n')
    print(f"wrote {out} ({out.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
