"""Standard product renders for the docs (uses the headless renderer)."""

from __future__ import annotations

from pathlib import Path

from build123d import Pos

from . import components, parts
from .layout import L as DEFAULT_LAYOUT
from .layout import Layout
from .render import Scene
from .style import PART_COLORS, component_color

NO_EDGES = ("pins", "stem")


def scene(L: Layout = DEFAULT_LAYOUT, explode: float = 0.0, plate=True, comps=True, caps=True, only=None) -> Scene:
    s = Scene()
    lift = {"top_plate": 2 * explode, "frame": explode, "base": 0.0, "bar_spine": 2 * explode}
    for name, part in parts.all_parts(L).items():
        if (name == "top_plate" and not plate) or (only and name not in only):
            continue
        s.add(Pos(0, 0, lift[name]) * part, PART_COLORS[name])
    if comps:
        for cname, pieces in components.placed_components(L).items():
            if cname.startswith("keycap") and not caps:
                continue
            if only and cname not in only and cname.split(" ")[0] not in only:
                continue
            dz = 0.0 if cname in ("pico", "joystick") else 2 * explode
            for pn, shape in pieces.items():
                s.add(Pos(0, 0, dz) * shape, component_color(cname.split(" ")[0], pn), edges=pn not in NO_EDGES)
    return s


def render_all(out: Path, L: Layout = DEFAULT_LAYOUT, verbose: bool = True) -> dict[str, Path]:
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    jobs = {
        "hero": (dict(), dict(eye_dir=(-0.5, -1.0, 0.72), zoom=1.05)),
        "exploded": (dict(explode=34), dict(eye_dir=(-0.55, -1.0, 0.55), zoom=1.0)),
        "back_usb": (dict(), dict(eye_dir=(0.35, 1.0, 0.35), zoom=1.1)),
        "inside": (dict(plate=False, caps=False), dict(eye_dir=(-0.3, -0.7, 1.0), zoom=1.05)),
        "side": (dict(), dict(eye_dir=(-1, 0, 0), ortho_view=True, shadows=False)),
        "top": (dict(), dict(eye_dir=(0, -1e-4, 1), ortho_view=True, up=(0, 1, 0), shadows=False)),
        "case_only": (dict(comps=False), dict(eye_dir=(-0.5, -1.0, 0.72), zoom=1.05)),
    }
    done = {}
    for name, (scene_kw, view_kw) in jobs.items():
        path = out / f"{name}.png"
        scene(L, **scene_kw).render(str(path), **view_kw)
        done[name] = path
        if verbose:
            print("  render", path.name)
    s = Scene()
    s.add(parts.top_plate(L), PART_COLORS["top_plate"])
    s.add(parts.bar_spine(L), PART_COLORS["bar_spine"])
    for cname, pieces in components.placed_components(L).items():
        if cname.startswith(("switch", "display")):
            for pn, shape in pieces.items():
                s.add(shape, component_color(cname.split(" ")[0], pn), edges=pn not in NO_EDGES)
    path = out / "plate_underside.png"
    s.render(str(path), eye_dir=(-0.35, -0.6, -1.0), zoom=1.05)
    done["plate_underside"] = path
    u = L.usb_center
    s = scene(L, only=("frame", "base", "top_plate", "pico"))
    path = out / "usb_detail.png"
    s.render(str(path), eye_dir=(0.45, 1.0, 0.25), target=(u.X, u.Y, u.Z), zoom=4.0)
    done["usb_detail"] = path
    return done
