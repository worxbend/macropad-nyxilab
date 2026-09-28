"""The edition interface: everything that differs between the joystick and the rotary edition.

The shared model (plate, frame, base, displays, keys, Pico, LED sticks) calls these
hooks.  Each edition lives in its own project and implements them:

    joystick/cad/edition.py   KY-023 thumbstick on posts in the base
    rotary/cad/edition.py     KY-040 encoder in the top plate + printed knob

A ``Layout`` carries its edition as ``L.ed``.  Hooks receive the layout so they can
use the shared frames (``L.ploc``, ``L.u_c``, ``L.v_c``) and the shared parameters.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Callable

from build123d import Part

if TYPE_CHECKING:
    from .drawings import Sheet, View
    from .layout import Layout


class Edition:
    # ---- identity ----------------------------------------------------------------
    name = "base"  # used in file names: nyxilab_macropad_<name>.FCStd, docs/images/<name>/
    title = ""  # drawing title block, e.g. "JOYSTICK EDITION"
    centre_label = ""  # what the centre control is called in the viewer / docs
    # ---- render / export hints ---------------------------------------------------
    part_colors: dict[str, str] = {}  # extra printed parts
    component_colors: dict[str, str] = {}  # extra component pieces
    component_overrides: dict[tuple[str, str], str] = {}
    print_notes: dict[str, str] = {}  # extra printed parts
    render_lift: dict[str, float] = {}  # extra part -> explode multiplier (2 = moves with the plate)
    base_mounted_components: tuple[str, ...] = ()  # component names that stay with the base when exploded
    plate_mounted_components: tuple[str, ...] = ()  # ... shown with the plate in the underside render
    underside_extra_parts: tuple[str, ...] = ()  # extra printed parts shown in the underside render
    sweep_exclude: tuple[str, ...] = ()  # printed parts not tested against the sweep (the thing that sweeps)
    tilt_label_frac = 0.45  # where the "10 deg" label sits on the side view (fraction of the depth)

    # ---- geometry ----------------------------------------------------------------
    def plate_cuts(self, L: "Layout") -> Part:
        """World-space solid subtracted from the top plate (the centre-control mount)."""
        raise NotImplementedError

    def base_features(self, L: "Layout", body: Part) -> Part:
        """Add posts / reliefs to the base.  Default: nothing."""
        return body

    def parts(self, L: "Layout") -> dict[str, Part]:
        """Extra printed parts in world coordinates (e.g. the knob)."""
        return {}

    def print_pose(self, name: str, part: Part, L: "Layout") -> Part | None:
        """Print orientation of an extra part (None = as is)."""
        return None

    def components(self, L: "Layout") -> dict[str, dict[str, Part]]:
        """Placed reference models of the centre control, grouped by component."""
        raise NotImplementedError

    def keepouts(self, L: "Layout") -> dict[str, Part]:
        raise NotImplementedError

    def sweep(self, L: "Layout") -> tuple[str, Part]:
        """(name, world solid) of the space the moving part needs."""
        raise NotImplementedError

    def fit_tests(self, L: "Layout") -> dict[str, Callable[["Layout"], Part]]:
        return {}

    def clearances(self, L: "Layout") -> dict[str, float]:
        return {}

    def top_z(self, L: "Layout") -> float:
        """Highest point of the assembled pad above the desk (default: the back edge)."""
        return L.H_b

    def summary(self, L: "Layout") -> dict:
        return {}

    def viewer_info(self, L: "Layout", clearances: dict[str, float]) -> dict:
        """Facts for docs/viewer.html: {"centre": ..., "height_extra": ..., "section_note": ...}."""
        return {}

    # ---- drawings ----------------------------------------------------------------
    def section_styles(self, comp: dict) -> list:
        """(shape, png face colour, sheet face colour, hatch) for the centre-control pieces cut by A-A."""
        return []

    def section_callouts(self, L: "Layout") -> list:
        """[((y, z), text)] on sheet 1's section (world y/z)."""
        return []

    def section_png_labels(self, L: "Layout") -> list:
        """[(y, z, text)] on the standalone section image."""
        return []

    def side_view_dims(self, s: "Sheet", side: "View", L: "Layout") -> None:
        """Extra dimensions on the general-arrangement side view."""

    def plate_notes(self, L: "Layout") -> list[str]:
        return []

    def plate_underside_notes(self, L: "Layout") -> list[str]:
        return []

    base_subtitle = "Pico 2 cradle, LED posts, M3 countersinks, feet"

    def base_notes(self, L: "Layout") -> list[str]:
        return []

    bom_printed: list[list[str]] = []  # rows [part, qty, spec] after the shared printed parts
    bom_hardware: list[list[str]] = []  # rows after the shared screws
    fit_lines_after_bar: list[str] = []  # fit-test coupon lines (sheet 5)
    fit_lines_after_usb: list[str] = []
    sheet5_title = "BAR DISPLAY SPINE & FIT TESTS"
    sheet5_subtitle = "Clamp for the 2.25in module; coupons to print first"
    sheet5_name = "05_spine_and_fit_tests"

    def sheet5_extra(self, s: "Sheet", L: "Layout") -> None:
        """Extra views on sheet 5 (e.g. the knob)."""
