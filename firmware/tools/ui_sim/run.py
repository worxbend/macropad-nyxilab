#!/usr/bin/env python3
"""Build the UI simulator with the host compiler and turn its output into PNGs.

    python3 tools/ui_sim/run.py [out_dir]

Needs the Adafruit GFX library that PlatformIO downloads (run `pio run` once).
"""
import glob
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FW = HERE.parents[1]
REPO = FW.parent


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "docs" / "images" / "ui"
    out.mkdir(parents=True, exist_ok=True)
    gfx = sorted(glob.glob(str(FW / ".pio" / "libdeps" / "*" / "Adafruit GFX Library")))
    if not gfx:
        print("Adafruit GFX not found - run `pio run` in firmware/ first")
        return 1
    gfx = Path(gfx[0])
    build = FW / ".pio" / "ui_sim"
    build.mkdir(parents=True, exist_ok=True)
    exe = build / "ui_sim"
    cmd = ["g++", "-std=gnu++17", "-O1", "-w", "-DARDUINO=100", f"-I{HERE / 'shim'}", f"-I{gfx}", f"-I{FW / 'src'}", f"-I{FW / 'include'}",
           str(HERE / "ui_sim.cpp"), str(FW / "src" / "ui" / "render.cpp"), str(FW / "src" / "core" / "keymap.cpp"),
           str(gfx / "Adafruit_GFX.cpp"), "-o", str(exe)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe), str(build)], check=True)
    from PIL import Image, ImageDraw

    scenes = sorted({p.name.rsplit("_", 1)[0] for p in build.glob("*_main.ppm")})
    for sc in scenes:
        main_img = Image.open(build / f"{sc}_main.ppm").resize((640, 340), Image.NEAREST)
        bar_img = Image.open(build / f"{sc}_bar.ppm").resize((152, 568), Image.NEAREST)
        W, H = 640 + 152 + 3 * 28, 568 + 2 * 28
        card = Image.new("RGB", (W, H), (236, 234, 228))
        d = ImageDraw.Draw(card)
        d.rounded_rectangle((16, 16, 16 + 152 + 24, 16 + 568 + 24), 10, fill=(20, 21, 26))
        card.paste(bar_img, (28, 28))
        mx, my = 28 + 152 + 28, 28 + (568 - 340) // 2
        d.rounded_rectangle((mx - 12, my - 12, mx + 640 + 12, my + 340 + 12), 10, fill=(20, 21, 26))
        card.paste(main_img, (mx, my))
        card.save(out / f"ui_{sc}.png")
        print("wrote", out / f"ui_{sc}.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
