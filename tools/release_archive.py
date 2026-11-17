#!/usr/bin/env python3
"""Pack the committed CAD outputs and renders into one release zip per edition.

    python3 tools/release_archive.py [VERSION]   -> dist/nyxilab-macropad-<edition>-<VERSION>.zip

Each zip holds 3mf/, step/, stl/ (print-ready, oriented on the bed), drawings/ (blueprints and
template_top_plate_1to1.pdf, the top plate printed at 100 % on paper), renders/ (docs/images/<edition>),
manifest.json (per-part print notes), and docs/nyxilab-build-guide.pdf. The same PDF is also copied
to dist/ so the release can attach it on its own. .github/workflows/release.yml runs this on a v* tag.
"""

from __future__ import annotations

import shutil
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DIST = REPO / "dist"
EDITIONS = ("joystick", "rotary")
EXPORT_DIRS = ("3mf", "step", "stl", "drawings")
GUIDE = REPO / "docs" / "nyxilab-build-guide.pdf"


def edition_files(edition: str) -> list[tuple[Path, str]]:
    """(source, path in the archive) for one edition; fails if an expected folder is missing."""
    exports = REPO / edition / "cad" / "exports"
    sources = {name: exports / name for name in EXPORT_DIRS}
    sources["renders"] = REPO / "docs" / "images" / edition
    files = [(exports / "manifest.json", "manifest.json")]
    for name, folder in sources.items():
        found = sorted(p for p in folder.rglob("*") if p.is_file())
        if not found:
            sys.exit(f"release_archive: nothing in {folder.relative_to(REPO)}")
        files += [(p, f"{name}/{p.relative_to(folder).as_posix()}") for p in found]
    if not GUIDE.is_file():
        sys.exit(f"release_archive: missing {GUIDE.relative_to(REPO)}")
    files.append((GUIDE, GUIDE.name))
    return files


def build(edition: str, version: str) -> Path:
    root = f"nyxilab-macropad-{edition}-{version}"
    out = DIST / f"{root}.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc in edition_files(edition):
            z.write(src, f"{root}/{arc}")
    return out


def main() -> None:
    version = sys.argv[1] if len(sys.argv) > 1 else "dev"
    DIST.mkdir(exist_ok=True)
    for edition in EDITIONS:
        out = build(edition, version)
        print(f"{out.relative_to(REPO)}  {out.stat().st_size / 1e6:.1f} MB")
    shutil.copyfile(GUIDE, DIST / GUIDE.name)
    print(f"{(DIST / GUIDE.name).relative_to(REPO)}  {GUIDE.stat().st_size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
