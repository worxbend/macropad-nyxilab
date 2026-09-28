"""Load the editions' CAD modules for the tools (wiring map, viewer, ...).

    from editions import layouts
    L = layouts()["rotary"]

Both editions define a module called ``edition`` (``joystick/cad/edition.py``,
``rotary/cad/edition.py``), so they are imported under distinct names here.
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
EDITIONS = ("joystick", "rotary")

if str(REPO / "common" / "cad") not in sys.path:
    sys.path.insert(0, str(REPO / "common" / "cad"))


def load(name: str):
    """The Edition object of ``joystick`` or ``rotary``."""
    spec = importlib.util.spec_from_file_location(f"{name}_edition", REPO / name / "cad" / "edition.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod  # dataclasses need the module registered before it runs
    spec.loader.exec_module(mod)
    return mod.EDITION


def layouts() -> dict:
    from macropad_cad.layout import Layout

    return {name: Layout(load(name)) for name in EDITIONS}
