#!/usr/bin/env python3
"""Embed cad/exports/gltf/macropad_assembly.glb into docs/viewer.html (self-contained page).

    python3 docs/tools/build_viewer.py
"""
import base64
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
tpl = (REPO / "docs" / "tools" / "viewer_template.html").read_text()
glb = (REPO / "cad" / "exports" / "gltf" / "macropad_assembly.glb").read_bytes()
page = tpl.replace("__GLB_BASE64__", base64.b64encode(glb).decode("ascii"))
out = REPO / "docs" / "viewer.html"
# standalone copy: add the document skeleton the artifact host would otherwise provide
out.write_text('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
               '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
               '</head>\n<body>\n' + page + '\n</body>\n</html>\n')
print(f"wrote {out} ({out.stat().st_size / 1e6:.1f} MB)")
