"""Automated fit checks.

Every printed part is tested against every component keep-out volume
(component + room for solder joints and hand wiring), against the sweep of the
centre control (the joystick knob's tilt, or the spinning knob), and against the
other printed parts.  Booleans are done on meshes with manifold3d, which is fast
enough to run on every build.
"""

from __future__ import annotations

import itertools
from dataclasses import dataclass

import manifold3d as m3d
import numpy as np

from . import components, parts
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


def run(L: Layout, verbose: bool = True) -> list[Result]:
    printed = {n: to_manifold(s) for n, s in parts.all_parts(L).items()}
    keepouts = {n: to_manifold(s) for n, s in components.placed_keepouts(L).items()}
    sweep_name, sweep_shape = L.ed.sweep(L)
    sweep = to_manifold(sweep_shape)
    results: list[Result] = []

    # printed parts must not intrude into any component keep-out
    allow = {("bar_spine", "display bar"): 35.0}  # 0.2 mm preload of the clamp pad
    for (pn, pm), (kn, km) in itertools.product(printed.items(), keepouts.items()):
        results.append(Result(pn, kn, overlap(pm, km), allow.get((pn, kn), 0.05)))
    for pn, pm in printed.items():
        if pn not in L.ed.sweep_exclude:  # (the part that sweeps is not tested against itself)
            results.append(Result(pn, sweep_name, overlap(pm, sweep), 0.05))
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


def clearance_report(L: Layout) -> dict[str, float]:
    """Key analytic clearances (mm) that the booleans do not express directly."""
    p = L.p
    c, fas, tol = p.case, p.fas, p.tol
    out = dict(L.ed.clearances(L))
    # LED sticks: joints under the end pads, and the gap to anything else (parts, modules, wiring)
    out["LED stick joints above base"] = c.led_post_h - p.led.joints_below
    kos = {n: to_manifold(s) for n, s in components.placed_keepouts(L).items()}
    others = [m for n, m in kos.items() if not n.startswith("led")]
    others += [to_manifold(s) for n, s in parts.all_parts(L).items() if n != "base"]
    out["LED sticks to nearest neighbour"] = min(kos[n].min_gap(m, 20.0) for n in L.led_locs for m in others)
    # front row switch wiring above the base
    key = L.keys[0]
    s = p.sw
    lowest = L.world(key.u, key.v - s.body / 2, -(s.body_below + s.pins_below + s.wiring_below))
    out["front switch wiring above base"] = lowest.Z - c.base_t
    out["frame wall behind boss"] = c.boss_inset - fas.boss_d / 2 - tol.fit - 0.05
    out["front height"] = L.H_f
    out["back height"] = L.H_b
    out["boss insert engagement (M3x8)"] = fas.case_screw_len - c.base_t - tol.boss_gap
    return out


def overhang_report(L: Layout, max_angle_deg: float = 50.0, bed_z: float = 0.3) -> dict[str, dict]:
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
