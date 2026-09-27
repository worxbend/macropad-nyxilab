"""Headless OpenGL renderer (moderngl + EGL) for design reviews and docs.

Shapes are tessellated face by face (smooth normals inside a face, sharp
creases between faces), lit by a shadow-casting key light plus fill/ambient,
and optionally outlined with their B-rep edges for a clean CAD look.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
from PIL import Image

try:  # optional at import time so the rest of the package works without a GPU
    import moderngl
except ImportError:  # pragma: no cover
    moderngl = None


def hex_rgb(h: str) -> tuple[float, float, float]:
    h = h.lstrip("#")
    return tuple(int(h[i : i + 2], 16) / 255 for i in (0, 2, 4))


def tessellate(shape, tol: float = 0.03, ang: float = 0.2):
    """Per-face tessellation with face-local smooth normals."""
    verts, norms, tris = [], [], []
    base = 0
    for face in shape.faces():
        try:
            v, t = face.tessellate(tol, ang)
        except Exception:
            continue
        if not t:
            continue
        v = np.array([(p.X, p.Y, p.Z) for p in v], dtype=np.float64)
        t = np.array(t, dtype=np.int64)
        fn = np.cross(v[t[:, 1]] - v[t[:, 0]], v[t[:, 2]] - v[t[:, 0]])
        vn = np.zeros_like(v)
        for k in range(3):
            np.add.at(vn, t[:, k], fn)
        ln = np.linalg.norm(vn, axis=1, keepdims=True)
        vn = np.where(ln > 1e-12, vn / np.maximum(ln, 1e-12), 0)
        # make sure normals face outward: compare with the B-rep normal at the centre
        try:
            ref = face.normal_at()
            c = v.mean(axis=0)
            if (fn.sum(axis=0) @ np.array([ref.X, ref.Y, ref.Z])) < 0:
                t = t[:, ::-1]
                vn = -vn
        except Exception:
            pass
        verts.append(v)
        norms.append(vn)
        tris.append(t + base)
        base += len(v)
    if not verts:
        return np.zeros((0, 3)), np.zeros((0, 3)), np.zeros((0, 3), dtype=np.int64)
    return np.vstack(verts), np.vstack(norms), np.vstack(tris)


def edge_segments(shape, max_seg: float = 1.0):
    segs = []
    for e in shape.edges():
        try:
            n = 1 if e.geom_type.name == "LINE" else max(4, min(64, int(e.length / max_seg) + 1))
            pts = [e.position_at(i / n) for i in range(n + 1)]
        except Exception:
            continue
        for a, b in zip(pts[:-1], pts[1:]):
            segs.append((a.X, a.Y, a.Z))
            segs.append((b.X, b.Y, b.Z))
    return np.array(segs, dtype=np.float64).reshape(-1, 3)


def look_at(eye, target, up):
    eye, target, up = map(lambda a: np.asarray(a, dtype=np.float64), (eye, target, up))
    f = target - eye
    f /= np.linalg.norm(f)
    s = np.cross(f, up)
    s /= np.linalg.norm(s)
    u = np.cross(s, f)
    m = np.eye(4)
    m[0, :3], m[1, :3], m[2, :3] = s, u, -f
    m[:3, 3] = -m[:3, :3] @ eye
    return m


def perspective(fovy_deg, aspect, near, far):
    f = 1 / math.tan(math.radians(fovy_deg) / 2)
    m = np.zeros((4, 4))
    m[0, 0], m[1, 1] = f / aspect, f
    m[2, 2] = (far + near) / (near - far)
    m[2, 3] = 2 * far * near / (near - far)
    m[3, 2] = -1
    return m


def ortho(l, r, b, t, n, f):
    m = np.eye(4)
    m[0, 0], m[1, 1], m[2, 2] = 2 / (r - l), 2 / (t - b), -2 / (f - n)
    m[0, 3], m[1, 3], m[2, 3] = -(r + l) / (r - l), -(t + b) / (t - b), -(f + n) / (f - n)
    return m


VS = """
#version 330
uniform mat4 mvp; uniform mat4 light_mvp;
in vec3 in_pos; in vec3 in_norm; in vec3 in_color;
out vec3 v_norm; out vec3 v_color; out vec4 v_lpos; out vec3 v_world;
void main(){ gl_Position = mvp*vec4(in_pos,1.0); v_norm=in_norm; v_color=in_color;
  v_lpos = light_mvp*vec4(in_pos,1.0); v_world=in_pos; }
"""

FS = """
#version 330
uniform vec3 key_dir; uniform vec3 fill_dir; uniform vec3 eye; uniform int use_shadow;
uniform sampler2DShadow shadow_map; uniform float shadow_px;
in vec3 v_norm; in vec3 v_color; in vec4 v_lpos; in vec3 v_world;
out vec4 f_color;
float shadow(){
  if(use_shadow==0) return 1.0;
  vec3 p = v_lpos.xyz / v_lpos.w * 0.5 + 0.5;
  if(p.x<0.0||p.x>1.0||p.y<0.0||p.y>1.0) return 1.0;
  float s=0.0;
  for(int i=-2;i<=2;i++) for(int j=-2;j<=2;j++)
    s += texture(shadow_map, vec3(p.xy+vec2(i,j)*shadow_px, p.z-0.0015));
  return s/25.0;
}
void main(){
  vec3 n = normalize(v_norm);
  vec3 V = normalize(eye - v_world);
  if(dot(n,V)<0.0) n=-n;
  float kd = max(dot(n, key_dir),0.0);
  float fd = max(dot(n, fill_dir),0.0);
  vec3 H = normalize(key_dir + V);
  float sp = pow(max(dot(n,H),0.0), 48.0);
  float hemi = 0.5 + 0.5*n.z;
  float sh = shadow();
  vec3 c = v_color*(0.26 + 0.20*hemi) + v_color*kd*0.62*sh + v_color*fd*0.20 + vec3(0.16)*sp*sh;
  float rim = pow(1.0-max(dot(n,V),0.0), 3.0)*0.10;
  f_color = vec4(pow(c + rim, vec3(1.0/1.12)), 1.0);
}
"""

DEPTH_VS = """
#version 330
uniform mat4 light_mvp; in vec3 in_pos;
void main(){ gl_Position = light_mvp*vec4(in_pos,1.0); }
"""
DEPTH_FS = "#version 330\nvoid main(){}"

LINE_VS = """
#version 330
uniform mat4 mvp; in vec3 in_pos;
void main(){ gl_Position = mvp*vec4(in_pos,1.0); gl_Position.z -= 0.00008*gl_Position.w; }
"""
LINE_FS = """
#version 330
uniform vec4 color; out vec4 f_color; void main(){ f_color = color; }
"""


@dataclass
class _Item:
    v: np.ndarray
    n: np.ndarray
    t: np.ndarray
    color: tuple
    edges: np.ndarray | None
    edge_color: tuple


class Scene:
    def __init__(self):
        self.items: list[_Item] = []

    def add(self, shape, color, edges: bool = True, edge_color=(0.08, 0.08, 0.1, 0.55), tol=0.03):
        if isinstance(color, str):
            color = hex_rgb(color)
        v, n, t = tessellate(shape, tol)
        e = edge_segments(shape) if edges else None
        self.items.append(_Item(v, n, t, color, e, edge_color))

    def bounds(self):
        allv = np.vstack([i.v for i in self.items if len(i.v)])
        return allv.min(axis=0), allv.max(axis=0)

    def render(self, path, eye_dir=(-0.55, -1.0, 0.75), target=None, fov=24.0, size=(1800, 1150),
               bg=("#f4f3f0", "#dedcd6"), ortho_view=False, dist_scale=1.0, zoom=1.0, ssaa=2,
               key_dir=(-0.35, -0.45, 1.0), shadows=True, line_width=1.6, up=(0, 0, 1)):
        if moderngl is None:
            raise RuntimeError("moderngl is not installed")
        lo, hi = self.bounds()
        ctr = (lo + hi) / 2 if target is None else np.asarray(target, dtype=float)
        radius = float(np.linalg.norm(hi - lo)) / 2
        W, H = size[0] * ssaa, size[1] * ssaa
        ctx = moderngl.create_context(standalone=True, backend="egl")
        ctx.enable(moderngl.DEPTH_TEST)

        ed = np.asarray(eye_dir, dtype=float)
        ed /= np.linalg.norm(ed)
        aspect = W / H
        if ortho_view:
            dist = radius * 4
            eye = ctr + ed * dist
            view = look_at(eye, ctr, up)
            h = radius * 1.08 / zoom
            proj = ortho(-h * aspect, h * aspect, -h, h, 0.1, dist * 3)
        else:
            dist = radius / math.sin(math.radians(fov) / 2) * 1.02 * dist_scale / zoom
            eye = ctr + ed * dist
            view = look_at(eye, ctr, up)
            proj = perspective(fov, aspect, dist * 0.05, dist * 4)
        mvp = (proj @ view).astype("f4")

        kd = np.asarray(key_dir, dtype=float)
        kd /= np.linalg.norm(kd)
        fill = np.array([0.8, -0.3, 0.45])
        fill /= np.linalg.norm(fill)
        lview = look_at(ctr + kd * radius * 4, ctr, (0, 1, 0) if abs(kd[2]) > 0.9 else (0, 0, 1))
        lproj = ortho(-radius * 1.3, radius * 1.3, -radius * 1.3, radius * 1.3, 0.1, radius * 8)
        lmvp = (lproj @ lview).astype("f4")

        prog = ctx.program(vertex_shader=VS, fragment_shader=FS)
        dprog = ctx.program(vertex_shader=DEPTH_VS, fragment_shader=DEPTH_FS)
        lprog = ctx.program(vertex_shader=LINE_VS, fragment_shader=LINE_FS)

        vaos, dvaos, lvaos = [], [], []
        for it in self.items:
            if not len(it.t):
                continue
            col = np.tile(np.asarray(it.color, dtype="f4"), (len(it.v), 1))
            data = np.hstack([it.v.astype("f4"), it.n.astype("f4"), col]).astype("f4")
            vbo = ctx.buffer(data.tobytes())
            ibo = ctx.buffer(it.t.astype("i4").tobytes())
            vaos.append(ctx.vertex_array(prog, [(vbo, "3f 3f 3f", "in_pos", "in_norm", "in_color")], ibo))
            dvaos.append(ctx.vertex_array(dprog, [(vbo, "3f 12x 12x", "in_pos")], ibo))
            if it.edges is not None and len(it.edges):
                lvbo = ctx.buffer(it.edges.astype("f4").tobytes())
                lvaos.append((ctx.vertex_array(lprog, [(lvbo, "3f", "in_pos")]), it.edge_color))

        SM = 4096
        smap = ctx.depth_texture((SM, SM))
        smap.compare_func = "<="
        smap.repeat_x = smap.repeat_y = False
        sfbo = ctx.framebuffer(depth_attachment=smap)
        if shadows:
            sfbo.use()
            sfbo.clear(depth=1.0)
            dprog["light_mvp"].write(lmvp.T.tobytes())
            for va in dvaos:
                va.render()

        color_rb = ctx.renderbuffer((W, H), 4, samples=4)
        depth_rb = ctx.depth_renderbuffer((W, H), samples=4)
        fbo = ctx.framebuffer(color_rb, depth_rb)
        out_fbo = ctx.framebuffer(ctx.renderbuffer((W, H), 4))
        fbo.use()
        ctx.viewport = (0, 0, W, H)
        fbo.clear(0, 0, 0, 0, depth=1.0)
        smap.use(location=0)
        prog["shadow_map"] = 0
        prog["shadow_px"] = 1.0 / SM
        prog["use_shadow"] = 1 if shadows else 0
        prog["mvp"].write(mvp.T.tobytes())
        prog["light_mvp"].write(lmvp.T.tobytes())
        prog["key_dir"].value = tuple(kd)
        prog["fill_dir"].value = tuple(fill)
        prog["eye"].value = tuple(eye)
        for va in vaos:
            va.render()
        ctx.enable(moderngl.BLEND)
        ctx.blend_func = moderngl.SRC_ALPHA, moderngl.ONE_MINUS_SRC_ALPHA
        ctx.line_width = line_width * ssaa
        lprog["mvp"].write(mvp.T.tobytes())
        for va, colr in lvaos:
            lprog["color"].value = tuple(colr)
            va.render(mode=moderngl.LINES)
        ctx.copy_framebuffer(out_fbo, fbo)
        raw = out_fbo.read(components=4, alignment=1)
        img = np.frombuffer(raw, dtype=np.uint8).reshape(H, W, 4)[::-1]

        top, bot = (np.array(hex_rgb(c)) * 255 for c in bg)
        grad = (top[None, :] * (1 - np.linspace(0, 1, H)[:, None]) + bot[None, :] * np.linspace(0, 1, H)[:, None])
        a = img[..., 3:4] / 255.0
        comp = img[..., :3] * a + grad[:, None, :] * (1 - a)
        out = Image.fromarray(comp.clip(0, 255).astype(np.uint8))
        if ssaa > 1:
            out = out.resize(size, Image.LANCZOS)
        out.save(path)
        ctx.release()
        return path
