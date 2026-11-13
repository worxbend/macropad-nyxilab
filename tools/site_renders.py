#!/usr/bin/env python3
"""Renders for the product page (web/index.html), on black, as WebP.

    python tools/site_renders.py          -> docs/images/site/

Two scroll-scrubbed frame sequences (the case coming apart, the pad turning over
to show the signature underneath) and the stills the page shows.  Needs EGL, like
the CAD renders.  The outputs are committed, so the Pages workflow does not render.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "docs" / "images" / "site"
BLACK = ("#000000", "#000000")
SIZE = (1600, 1000)
EXPLODE_FRAMES = 36
FLIP_FRAMES = 36
EXPLODE_MAX = 34.0


def layout(edition: str):
    """Fresh imports per edition: both editions ship a module called `edition`."""
    for name in [m for m in sys.modules if m == "edition" or m.startswith("macropad_cad")]:
        del sys.modules[name]
    sys.path[:0] = [str(REPO / edition / "cad"), str(REPO / "common" / "cad")]
    from edition import EDITION
    from macropad_cad import renders
    from macropad_cad.layout import Layout

    del sys.path[:2]
    return Layout(EDITION), renders


def save(scene, name: str, **view) -> None:
    png = OUT / f"{name}.png"
    view = {"bg": BLACK, "size": SIZE, **view}
    scene.render(str(png), **view)
    Image.open(png).save(OUT / f"{name}.webp", quality=82, method=6)
    png.unlink()
    print("  ", name)


def explode_sequence(L, renders) -> None:
    """The closed pad opening into its three layers, eased so the ends linger."""
    for i in range(EXPLODE_FRAMES):
        t = i / (EXPLODE_FRAMES - 1)
        eased = t * t * (3 - 2 * t)
        save(renders.scene(L, explode=EXPLODE_MAX * eased), f"explode_{i:02d}",
             eye_dir=(-0.55, -1.0, 0.55), shadows=False)


def flip_sequence(L, renders) -> None:
    """The pad turned over sideways, about the world y axis, the way the engraving is meant
    to be read.  The camera, its up vector and the key light turn the other way round the
    still model, which looks the same as turning the pad and keeps the underside lit."""
    scene = renders.scene(L)

    def turn(v, a):  # rotate about world y
        x, y, z = v
        return (x * math.cos(a) + z * math.sin(a), y, -x * math.sin(a) + z * math.cos(a))

    for i in range(FLIP_FRAMES):
        t = i / (FLIP_FRAMES - 1)
        a = -math.pi * t * t * (3 - 2 * t)
        save(scene, f"flip_{i:02d}", eye_dir=turn((-0.25, -1.0, 0.75), a), up=turn((0, 0, 1), a),
             key_dir=turn((-0.35, -0.45, 1.0), a), shadows=False, zoom=1.1 - 0.35 * math.sin(math.pi * t) + 0.4 * t * t)


def stills(edition: str, L, renders) -> None:
    views = {
        "hero": (dict(), dict(eye_dir=(-0.5, -1.0, 0.72), zoom=1.1)),
        "top": (dict(), dict(eye_dir=(0, -1e-4, 1), ortho_view=True, up=(0, 1, 0))),
        "side": (dict(), dict(eye_dir=(-1, 0, 0), ortho_view=True)),
        "back": (dict(), dict(eye_dir=(0.35, 1.0, 0.35), zoom=1.1)),
        "inside": (dict(plate=False, caps=False), dict(eye_dir=(-0.3, -0.7, 1.0), zoom=1.1)),
        "base_leds": (dict(only=("base", "pico", "led", *L.ed.base_mounted_components)),
                      dict(eye_dir=(-0.3, -0.8, 1.0), zoom=1.15)),
    }
    for name, (scene_kw, view_kw) in views.items():
        save(renders.scene(L, **scene_kw), f"{edition}_{name}", shadows=False, **view_kw)
    if edition == "joystick":
        save(renders.scene(L), "signature", eye_dir=(0.0, -0.3, -1.0), up=(0, 1, 0), zoom=1.7,
             key_dir=(0.35, -0.45, -1.0), shadows=False)


def ui_on_black(src: Path, dst: Path) -> None:
    """A display screenshot from tools/ui_sim with its light page around the panels turned black.

    Only the region connected to the corners is touched (so light text inside the panels stays),
    widened by 2 px to catch the anti-aliased panel edges; those pixels lose the light share
    they were blended with."""
    import numpy as np
    from PIL import ImageDraw, ImageFilter

    rgb = Image.open(src).convert("RGB")
    bg = np.array(rgb.getpixel((0, 0)), dtype=float)
    marked = rgb.copy()
    for corner in ((0, 0), (rgb.width - 1, 0), (0, rgb.height - 1), (rgb.width - 1, rgb.height - 1)):
        ImageDraw.floodfill(marked, corner, (255, 0, 255), thresh=12)
    outside = Image.fromarray((np.array(marked) == (255, 0, 255)).all(axis=2).astype(np.uint8) * 255)
    outside = np.array(outside.filter(ImageFilter.MaxFilter(5))) > 0
    px = np.array(rgb, dtype=float)
    light = px.mean(axis=2)
    dark = 20.0
    share_of_bg = np.clip((light - dark) / (bg.mean() - dark), 0, 1)[..., None]
    cleaned = np.clip(px - share_of_bg * bg, 0, 255)
    px[outside] = cleaned[outside]
    Image.fromarray(px.astype(np.uint8)).save(dst, quality=88, method=6)


def ui_screens() -> None:
    for edition in ("joystick", "rotary"):
        for src in sorted((REPO / "docs" / "images" / edition / "ui").glob("*.png")):
            ui_on_black(src, OUT / f"{edition}_{src.stem}.webp")
    print("   ui screens")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    ui_screens()
    if "--ui-only" in sys.argv:
        return
    for edition in ("joystick", "rotary"):
        print(edition)
        L, renders = layout(edition)
        stills(edition, L, renders)
        if edition == "joystick":
            explode_sequence(L, renders)
            flip_sequence(L, renders)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
