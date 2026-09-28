#!/usr/bin/env python3
"""Build the UI simulator with the host compiler and turn its output into PNGs.

    python3 tools/ui_sim/run.py [edition ...]        (default: both editions)

The joystick edition's UI goes to docs/images/joystick/ui/, the rotary edition's
to docs/images/rotary/ui/.  Every card also shows the 16 RGB LEDs as the
firmware would light them in that scene.  The scenes of an edition live in
<edition>/firmware/tools/ui_scenes.cpp.
Needs the Adafruit GFX library that PlatformIO downloads (run `pio run` in one
of the firmware projects once).
"""
import glob
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
LIB = REPO / "common" / "firmware" / "lib"
EDITIONS = ("joystick", "rotary")

# rotary's knob.h declares the presets that its scenes use
EXTRA_SOURCES = {"joystick": [], "rotary": []}


def led_strip(card, leds, mode, x0, y0, width):
    """The two sticks of eight as glowing dots (display gamma, so dim PWM values stay visible)."""
    from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

    d = ImageDraw.Draw(card)
    glow = Image.new("RGB", card.size, (0, 0, 0))
    g = ImageDraw.Draw(glow)
    pitch = (width - 60) / 16
    for i, (r, gg, b) in enumerate(leds):
        x = x0 + 30 + pitch * (i + 0.5) + (12 if i >= 8 else -12)  # small gap between the sticks
        disp = tuple(int(255 * (v / 255) ** 0.45) for v in (r, gg, b))
        lit = any(v > 2 for v in (r, gg, b))
        d.rounded_rectangle((x - 13, y0 - 13, x + 13, y0 + 13), 4, fill=(34, 36, 42))
        d.ellipse((x - 8, y0 - 8, x + 8, y0 + 8), fill=disp if lit else (56, 58, 66))
        if lit:
            g.ellipse((x - 16, y0 - 16, x + 16, y0 + 16), fill=tuple(v // 2 for v in disp))
    card.paste(ImageChops.add(card, glow.filter(ImageFilter.GaussianBlur(8))))
    dark = not any(any(v > 2 for v in px) for px in leds)
    text = f"RGB LEDs: {mode.lower()}" + (" (dark while the host sleeps)" if dark and mode != "OFF" else "")
    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 15)
    except OSError:
        font = ImageFont.load_default()
    ImageDraw.Draw(card).text((x0 + 18, y0 + 20), text, fill=(128, 132, 146), font=font)


def build(edition: str, gfx: Path) -> Path:
    fw = REPO / edition / "firmware"
    build_dir = REPO / ".build" / "ui_sim" / edition
    build_dir.mkdir(parents=True, exist_ok=True)
    for old in build_dir.glob("*"):
        old.unlink()
    exe = build_dir / "ui_sim"
    cmd = ["g++", "-std=gnu++17", "-O1", "-w", "-DARDUINO=100",
           f"-I{HERE / 'shim'}", f"-I{HERE}", f"-I{gfx}", f"-I{LIB / 'nyx_core' / 'src'}", f"-I{LIB / 'nyx_ui' / 'src'}",
           f"-I{fw / 'include'}", f"-I{fw / 'src'}",
           str(HERE / "ui_sim.cpp"), str(fw / "tools" / "ui_scenes.cpp"), str(fw / "src" / "keymap.cpp"),
           str(LIB / "nyx_ui" / "src" / "ui" / "render.cpp"), str(LIB / "nyx_core" / "src" / "core" / "led_fx.cpp"),
           str(gfx / "Adafruit_GFX.cpp"), "-o", str(exe)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe), str(build_dir)], check=True)
    return build_dir


def main() -> int:
    editions = sys.argv[1:] or list(EDITIONS)
    gfx = sorted(glob.glob(str(REPO / "*" / "firmware" / ".pio" / "libdeps" / "*" / "Adafruit GFX Library")))
    if not gfx:
        print("Adafruit GFX not found - run `pio run` in joystick/firmware or rotary/firmware first")
        return 1
    gfx = Path(gfx[0])
    from PIL import Image, ImageDraw

    for edition in editions:
        out = REPO / "docs" / "images" / edition / "ui"
        out.mkdir(parents=True, exist_ok=True)
        build_dir = build(edition, gfx)
        scenes = sorted({p.name.rsplit("_", 1)[0] for p in build_dir.glob("*_main.ppm")})
        for sc in scenes:
            main_img = Image.open(build_dir / f"{sc}_main.ppm").resize((640, 340), Image.NEAREST)
            bar_img = Image.open(build_dir / f"{sc}_bar.ppm").resize((152, 568), Image.NEAREST)
            lines = (build_dir / f"{sc}_leds.txt").read_text().split("\n")
            mode, leds = lines[0], [tuple(int(v) for v in ln.split()) for ln in lines[1:] if ln.strip()]
            W, H = 640 + 152 + 3 * 28, 568 + 2 * 28
            card = Image.new("RGB", (W, H), (236, 234, 228))
            d = ImageDraw.Draw(card)
            d.rounded_rectangle((16, 16, 16 + 152 + 24, 16 + 568 + 24), 10, fill=(20, 21, 26))
            card.paste(bar_img, (28, 28))
            mx, my = 28 + 152 + 28, 28 + (568 - 340) // 2 - 40
            d.rounded_rectangle((mx - 12, my - 12, mx + 640 + 12, my + 340 + 12), 10, fill=(20, 21, 26))
            card.paste(main_img, (mx, my))
            sy = my + 340 + 44
            d.rounded_rectangle((mx - 12, sy - 26, mx + 640 + 12, sy + 48), 10, fill=(20, 21, 26))
            led_strip(card, leds, mode, mx, sy, 640)
            card.save(out / f"ui_{sc}.png")
            print("wrote", out / f"ui_{sc}.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
