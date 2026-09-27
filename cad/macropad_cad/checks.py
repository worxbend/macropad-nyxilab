"""Automated fit checks.

Every printed part is tested against every component keep-out volume
(component + room for solder joints and hand wiring), against the joystick
knob's full sweep, and against the other printed parts.  Booleans are done
on meshes with manifold3d, which is fast enough to run on every build.

Run:  python -m macropad_cad.checks
"""

from __future__ import annotations

import itertools
import sys
from dataclasses import dataclass

import manifold3d as m3d
import numpy as np

from . import components, parts
from .layout import L as DEFAULT_LAYOUT
from .layout import Layout


def to_manifold(shape, tol: float = 0.02) -> m3d.Manifold:
    verts, tris = shape.tessellate(tol, 0.15)
    v = np.array([(p.X, p.Y, p.Z) for p in verts], dtype=np.float32)
    t = np.array(tris, dtype=np.uint32)
    mesh = m3d.Mesh(vert_properties=v, tri_verts=t)
    mesh.merge()
    man = m3d.Manifold(mesh)
    if man.status() != m3d.Error.NoError:
        raise ValueError(f"mesh is not manifold: {man.status()}")
    return man


@dataclass
class Result:
    a: str
    b: str
    volume: float
    limit: float

    @property
    def ok(self) -> bool:
        return self.volume <= self.limit


def overlap(a: m3d.Manifold, b: m3d.Manifold) -> float:
    return (a ^ b).volume()


def run(L: Layout = DEFAULT_LAYOUT, verbose: bool = True) -> list[Result]:
    p = L.p
    printed = {n: to_manifold(s) for n, s in parts.all_parts(L).items()}
    keepouts = {n: to_manifold(s) for n, s in components.placed_keepouts(L).items()}
    sweep = to_manifold(L.loc_joy * components.knob_sweep(p))
    results: list[Result] = []

    # printed parts must not intrude into any component keep-out
    allow = {("bar_spine", "display bar"): 35.0}  # 0.2 mm preload of the clamp pad
    for (pn, pm), (kn, km) in itertools.product(printed.items(), keepouts.items()):
        results.append(Result(pn, kn, overlap(pm, km), allow.get((pn, kn), 0.05)))
    for pn, pm in printed.items():
        results.append(Result(pn, "joystick knob sweep", overlap(pm, sweep), 0.05))
    # printed parts must not overlap each other (they only touch)
    for (an, am), (bn, bm) in itertools.combinations(printed.items(), 2):
        results.append(Result(an, bn, overlap(am, bm), 0.05))
    # components must not collide with each other (hand wiring included)
    for (an, am), (bn, bm) in itertools.combinations(keepouts.items(), 2):
        if an.startswith("switch") and bn.startswith("switch"):
            continue
        results.append(Result(an, bn, overlap(am, bm), 0.05))

    if verbose:
        bad = [r for r in results if not r.ok]
        print(f"fit checks: {len(results)} pairs, {len(bad)} failing")
        for r in sorted(bad, key=lambda r: -r.volume):
            print(f"  FAIL {r.a:>14} x {r.b:<22} overlap {r.volume:8.3f} mm3")
    return results


def clearance_report(L: Layout = DEFAULT_LAYOUT) -> dict[str, float]:
    """Key analytic clearances (mm) that the booleans do not express directly."""
    p = L.p
    c, j, pc, fas, tol = p.case, p.joy, p.pico, p.fas, p.tol
    out = {}
    # joystick PCB underside (incl. joints) above the base, at its lowest (front) edge
    front = (L.loc_joy * components.Pos(0, j.pcb_cy - j.pcb_w / 2, -j.pcb_t - j.joints_below)).position
    out["joystick joints above base"] = front.Z - (c.base_t - c.joy_base_relief)
    # front row switch wiring above the base
    k = L.keys[0]
    s = p.sw
    lowest = L.world(k.u, k.v - s.body / 2, -(s.body_below + s.pins_below + s.wiring_below))
    out["front switch wiring above base"] = lowest.Z - c.base_t
    # knob sweep radius vs hole at the plate underside
    import math

    h_under = c.joy_g + j.pivot_below_frame - c.plate_t
    r_sweep = math.sqrt(max((j.dome_r + 0.4) ** 2 - h_under**2, 0))
    out["joystick hole radial clearance"] = c.joy_hole_d / 2 - r_sweep
    # frame wall remaining behind a boss saddle
    out["frame wall behind boss"] = fas.boss_d / 2 * 0 + (c.boss_inset - fas.boss_d / 2 - tol.fit - 0.05)
    # heights
    out["front height"] = L.H_f
    out["back height"] = L.H_b
    out["boss insert engagement (M3x8)"] = fas.case_screw_len - c.base_t - tol.boss_gap
    return out


def main() -> int:
    res = run()
    for k, v in clearance_report().items():
        print(f"  {k:<36} {v:7.2f} mm")
    return 0 if all(r.ok for r in res) else 1


if __name__ == "__main__":
    sys.exit(main())


def overhang_report(max_angle_deg: float = 50.0, bed_z: float = 0.3, L: Layout = DEFAULT_LAYOUT) -> dict[str, dict]:
    """Downward-facing area steeper than ``max_angle_deg`` from vertical, per part,
    in print orientation.  Horizontal down-facing areas are bridges; the report
    lists their largest single patch so they can be judged (short bridges are fine).
    """
    import math

    import trimesh

    out = {}
    lim = -math.cos(math.radians(90 - max_angle_deg))
    for name, part in parts.all_parts(L).items():
        posed = parts.print_pose(name, part, L)
        v, t = posed.tessellate(0.05, 0.2)
        mesh = trimesh.Trimesh(np.array([(p.X, p.Y, p.Z) for p in v]), np.array(t), process=True)
        n = mesh.face_normals
        cz = mesh.triangles_center[:, 2]
        down = (n[:, 2] < lim) & (cz > bed_z)
        area = float(mesh.area_faces[down].sum())
        flat = down & (n[:, 2] < -0.995)
        # connected flat down-facing patches ~ bridges
        patches = []
        if flat.any():
            sub = mesh.submesh([np.nonzero(flat)[0]], append=True)
            for comp in sub.split(only_watertight=False):
                ext = comp.bounds[1] - comp.bounds[0]
                patches.append((round(float(comp.area), 1), round(float(min(ext[0], ext[1])), 1)))
        patches.sort(reverse=True)
        out[name] = {"steep_down_area_mm2": round(area, 1), "bridges(area,short_span)": patches[:6]}
    return out
