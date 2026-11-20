#!/usr/bin/python3
"""KiCad boards of the PCB version, built from pcb/<edition>/positions.json and pcb/netlist.json.

    /usr/bin/python3 tools/pcb_kicad.py [--route]        -> pcb/kicad/<board>/<board>.kicad_pcb

Runs with the system Python, which has KiCad's pcbnew module (make pcb first: it writes the inputs).
Boards: left and right (the same for both editions; J3 on the left board is fitted on the rotary edition
only), hat-rotary and hat-joystick (the joystick's dome cuts the hat's front).  Every footprint goes
where the fit check (tools/pcb_layout.py) placed it, turned so its cable or plug comes from the side the
fit check routed it from; the Pico 2 footprint sits on the hat's underside, matched pad by pad to the
Pico's pins.  Both copper layers get a GND fill.  --route runs Freerouting (the `freerouting` wrapper
around the 2.5.0 release jar; the native CLI build of 2.5.0 crashes its router thread and then hangs)
and imports its tracks.  KiCad's DRC report goes next to each board.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

import pcbnew

REPO = Path(__file__).resolve().parents[1]
PCB = REPO / "pcb"
FP = Path("/usr/share/kicad/footprints")
ORIGIN = (20.0, 120.0)  # board coordinates: x = 20 + u (or world x), y = 120 - v (or world y); KiCad's y runs down
EDGE_W = 0.1
TRACK = 0.2  # every net: the largest current is the LED sticks' 250 mA (0.2 mm, 1 oz carries ~0.6 A)
CLEARANCE = 0.2
EDGE_CLEARANCE = 0.25  # copper to board edge (JLCPCB: 0.2 minimum); the Pico's pins 1 and 40 sit 0.41 mm from the hat's back
MIN_TRACK = 0.15  # Freerouting may narrow a track to this where it squeezes past fine-pitch pads (JLCPCB: 0.127)
FOOTPRINTS = {
    "FPC20": ("Connector_FFC-FPC", "Hirose_FH12-20S-0.5SH_1x20-1MP_P0.50mm_Horizontal"),
    "SH8": ("Connector_JST", "JST_SH_SM08B-SRSS-TB_1x08-1MP_P1.00mm_Horizontal"),
    "SH5": ("Connector_JST", "JST_SH_SM05B-SRSS-TB_1x05-1MP_P1.00mm_Horizontal"),
    "PH3": ("Connector_JST", "JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal"),
    "MX": ("Button_Switch_Keyboard", "SW_Cherry_MX_1.00u_Plate"),  # plate-mounted: two pins and the centre post
    "D": ("Diode_SMD", "D_SOD-123"),
    "SOT": ("Package_TO_SOT_SMD", "SOT-23-5"),
    "R0402": ("Resistor_SMD", "R_0402_1005Metric"),
    "C0402": ("Capacitor_SMD", "C_0402_1005Metric"),
    "C0805": ("Capacitor_SMD", "C_0805_2012Metric"),
    "PICO": ("Module", "RaspberryPi_Pico_Common_THT"),
}
DIRECTION = {"+u": (1, 0), "-u": (-1, 0), "+x": (1, 0), "-x": (-1, 0), "-y": (0, 1), "+y": (0, -1)}


def mm(v: float) -> int:
    return pcbnew.FromMM(v)


def pt(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I(mm(x), mm(y))


def to_board(x: float, y: float) -> tuple[float, float]:
    return ORIGIN[0] + x, ORIGIN[1] - y


class Board:
    def __init__(self, name: str):
        self.name = name
        self.dir = PCB / "kicad" / name
        self.dir.mkdir(parents=True, exist_ok=True)
        self.path = self.dir / f"{name}.kicad_pcb"
        self.board = pcbnew.NewBoard(str(self.path))
        self.nets: dict[str, pcbnew.NETINFO_ITEM] = {}
        self.occupied: list[tuple[str, pcbnew.BOX2I]] = []
        ds = self.board.GetDesignSettings()
        ds.SetCopperLayerCount(2)
        ds.m_CopperEdgeClearance = mm(EDGE_CLEARANCE)
        ds.m_TrackMinWidth = mm(MIN_TRACK)
        nc = ds.m_NetSettings.GetDefaultNetclass()
        nc.SetTrackWidth(mm(TRACK))
        nc.SetClearance(mm(CLEARANCE))
        nc.SetViaDiameter(mm(0.6))
        nc.SetViaDrill(mm(0.3))

    def net(self, name: str):
        if name in ("", "NC", None):
            return None
        if name not in self.nets:
            n = pcbnew.NETINFO_ITEM(self.board, name)
            self.board.Add(n)
            self.nets[name] = n
        return self.nets[name]

    def outline(self, polygons: list) -> None:
        polygons = [clean(poly) for poly in polygons]
        for poly in polygons:
            pts = [to_board(x, y) for x, y in poly]
            for a, b in zip(pts, pts[1:] + pts[:1]):
                seg = pcbnew.PCB_SHAPE(self.board)
                seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
                seg.SetStart(pt(*a))
                seg.SetEnd(pt(*b))
                seg.SetLayer(pcbnew.Edge_Cuts)
                seg.SetWidth(mm(EDGE_W))
                self.board.Add(seg)
        self.polygons = [[to_board(x, y) for x, y in poly] for poly in polygons]

    def footprint(self, kind: str, ref: str, value: str, bottom: bool = False) -> pcbnew.FOOTPRINT:
        lib, name = FOOTPRINTS[kind]
        fp = pcbnew.FootprintLoad(str(FP / f"{lib}.pretty"), name)
        if fp is None:
            raise SystemExit(f"footprint {lib}:{name} not found")
        fp.SetReference(ref)
        fp.SetValue(value)
        self.board.Add(fp)
        if bottom:
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        return fp

    def connect(self, fp: pcbnew.FOOTPRINT, pins) -> None:
        """pins: list (pad 1..n) or dict pad number -> net; mounting pads (MP) stay unconnected."""
        items = pins.items() if isinstance(pins, dict) else ((str(i), n) for i, n in enumerate(pins, 1))
        for number, net in items:
            pads = [p for p in fp.Pads() if p.GetNumber() == str(number).split()[0]]
            if not pads:
                raise SystemExit(f"{fp.GetReference()}: no pad {number}")
            for p in pads:
                n = self.net(net)
                if n is not None:
                    p.SetNet(n)

    def courtyard(self, fp) -> pcbnew.BOX2I:
        layer = pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd
        return fp.GetCourtyard(layer).BBox()

    def face(self, fp, direction: str) -> None:
        """Turn a connector so its cable or plug enters from `direction` (its mounting pads are on that side)."""
        want = DIRECTION[direction]
        for deg in (0, 90, 180, 270):
            fp.SetOrientationDegrees(deg)
            sig = [p.GetPosition() for p in fp.Pads() if p.GetNumber() not in ("MP", "")]
            mp = [p.GetPosition() for p in fp.Pads() if p.GetNumber() == "MP"]
            sx, sy = sum(p.x for p in sig) / len(sig), sum(p.y for p in sig) / len(sig)
            mx, my = sum(p.x for p in mp) / len(mp), sum(p.y for p in mp) / len(mp)
            dx, dy = mx - sx, my - sy
            if dx * want[0] + dy * want[1] > 0.9 * (dx * dx + dy * dy) ** 0.5:
                return
        raise SystemExit(f"{fp.GetReference()}: no orientation takes the cable from {direction}")

    def centre_on(self, fp, x: float, y: float) -> None:
        """Move the footprint so its courtyard's centre lands on (x, y) (board coordinates, mm)."""
        c = self.courtyard(fp).GetCenter()
        p = fp.GetPosition()
        fp.SetPosition(pcbnew.VECTOR2I(p.x + mm(x) - c.x, p.y + mm(y) - c.y))
        self.occupied.append((fp.GetReference(), self.courtyard(fp)))

    def inside(self, box: pcbnew.BOX2I, margin: float = 0.4) -> bool:
        corners = [(box.GetLeft() - mm(margin), box.GetTop() - mm(margin)),
                   (box.GetRight() + mm(margin), box.GetTop() - mm(margin)),
                   (box.GetLeft() - mm(margin), box.GetBottom() + mm(margin)),
                   (box.GetRight() + mm(margin), box.GetBottom() + mm(margin))]
        return all(any(point_in(pcbnew.ToMM(cx), pcbnew.ToMM(cy), poly) for poly in self.polygons)
                   for cx, cy in corners)

    def place_near(self, fp, x: float, y: float, keep_off: list[pcbnew.BOX2I] = ()) -> None:
        """Put a small part at the free spot nearest (x, y): on the board, clear of every placed courtyard."""
        best = None
        for r in range(0, 400):
            for dx in range(-r, r + 1):
                for dy in (-r, r) if abs(dx) != r else range(-r, r + 1):
                    cx, cy = x + dx * 0.5, y + dy * 0.5
                    self.centre_probe(fp, cx, cy)
                    box = self.courtyard(fp)
                    others = [b for _, b in self.occupied] + list(keep_off)
                    if self.inside(box) and not any(box.Intersects(b) for b in others):
                        best = (cx, cy)
                        break
                if best:
                    break
            if best:
                break
        if not best:
            raise SystemExit(f"no room for {fp.GetReference()}")
        self.centre_on(fp, *best)

    def centre_probe(self, fp, x, y):
        c = self.courtyard(fp).GetCenter()
        p = fp.GetPosition()
        fp.SetPosition(pcbnew.VECTOR2I(p.x + mm(x) - c.x, p.y + mm(y) - c.y))

    def ground_fill(self) -> None:
        gnd = self.net("GND")
        for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
            for poly in self.polygons:
                z = pcbnew.ZONE(self.board)
                z.SetLayer(layer)
                z.SetNet(gnd)
                z.SetLocalClearance(mm(0.3))
                z.SetMinThickness(mm(0.25))
                z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
                outline = z.Outline()
                outline.NewOutline()
                for x, y in poly:
                    outline.Append(mm(x), mm(y))
                self.board.Add(z)

    def save(self, route: bool) -> dict:
        # Route before the GND fill exists: with a fill Freerouting treats GND as a plane and leaves it unrouted, and
        # the tracks then cut the fills into islands that nothing joins.
        self.board.Save(str(self.path))
        if route:
            self.route()
        self.ground_fill()
        self.board.Save(str(self.path))
        return self.drc()  # kicad-cli fills the zones and saves the board

    def route(self) -> None:
        exe = shutil.which("freerouting")
        if not exe:
            raise SystemExit("--route needs `freerouting` on PATH")
        dsn, ses = self.dir / f"{self.name}.dsn", self.dir / f"{self.name}.ses"
        nc = self.board.GetDesignSettings().m_NetSettings.GetDefaultNetclass()
        nc.SetClearance(mm(CLEARANCE + 0.01))  # Freerouting's rounding leaves tracks a few um inside the clearance
        exported = pcbnew.ExportSpecctraDSN(self.board, str(dsn))
        nc.SetClearance(mm(CLEARANCE))
        if not exported:
            raise SystemExit(f"{self.name}: Specctra export failed")
        log = self.dir / f"{self.name}-freerouting.log"
        # First without neckdown (Freerouting ignores its neck-width setting); if that leaves connections unrouted
        # (the joystick hat, where its dome narrows the board), again with it, and narrow tracks are widened after.
        for neckdown in (False, True):
            ses.unlink(missing_ok=True)
            with log.open("w") as out:  # a time limit: a hung router must not stall the build
                subprocess.run([exe, "--gui.enabled=false", "-de", str(dsn), "-do", str(ses),
                                "--router.autorouter.max_passes=100", f"--router.automatic_neckdown={str(neckdown).lower()}"],
                               check=True, cwd=self.dir, stdout=out, stderr=subprocess.STDOUT, timeout=900)
            if not ses.exists():
                raise SystemExit(f"{self.name}: Freerouting wrote no session; see {log}")
            if " (0 unrouted" in log.read_text():
                break
        if not pcbnew.ImportSpecctraSES(self.board, str(ses)):
            raise SystemExit(f"{self.name}: Specctra import failed")
        self.drop_dangling_vias()
        for t in self.board.GetTracks():  # neckdown goes below MIN_TRACK now and then; DRC checks the clearance after
            if not isinstance(t, pcbnew.PCB_VIA) and t.GetWidth() < mm(MIN_TRACK):
                t.SetWidth(mm(MIN_TRACK))
        self.board.Save(str(self.path))

    def drop_dangling_vias(self) -> None:
        """Freerouting leaves a via now and then that a track reaches on one layer only."""
        tracks = [t for t in self.board.GetTracks() if not isinstance(t, pcbnew.PCB_VIA)]
        for via in [t for t in self.board.GetTracks() if isinstance(t, pcbnew.PCB_VIA)]:
            pos = via.GetPosition()
            layers = {t.GetLayer() for t in tracks if t.GetNetCode() == via.GetNetCode()
                      and (t.GetStart() == pos or t.GetEnd() == pos)}
            if len(layers) < 2:
                self.board.Remove(via)

    def drc(self) -> dict:
        # a pad that its track already joins to GND needs no second thermal spoke (KiCad's default asks for two)
        (self.dir / f"{self.name}.kicad_dru").write_text(
            '(version 1)\n(rule "routed pads: one spoke is enough"\n  (condition "A.Type == \'Zone\'")\n'
            '  (constraint min_resolved_spokes 1))\n')
        report = self.dir / f"{self.name}-drc.json"
        subprocess.run(["kicad-cli", "pcb", "drc", "--refill-zones", "--save-board", "--format", "json",
                        "--severity-all", "-o", str(report), str(self.path)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        r = json.loads(report.read_text())
        kinds: dict[str, int] = {}
        for v in r.get("violations", []):
            kinds[v["type"]] = kinds.get(v["type"], 0) + 1
        return {"violations": kinds, "unconnected": len(r.get("unconnected_items", []))}


def clean(poly: list, eps: float = 0.01) -> list:
    """Drop points closer than eps to the previous one and points on a straight line between their neighbours:
    the offset outlines carry both, and KiCad reports them as zero-length, self-intersecting edges."""
    pts = []
    for x, y in poly:
        if not pts or abs(x - pts[-1][0]) > eps or abs(y - pts[-1][1]) > eps:
            pts.append((x, y))
    while len(pts) > 3 and abs(pts[0][0] - pts[-1][0]) <= eps and abs(pts[0][1] - pts[-1][1]) <= eps:
        pts.pop()
    out = []
    for i, (x, y) in enumerate(pts):
        (ax, ay), (bx, by) = pts[i - 1], pts[(i + 1) % len(pts)]
        cross = (x - ax) * (by - ay) - (y - ay) * (bx - ax)
        if abs(cross) > eps * ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5:
            out.append((x, y))
    return out


def point_in(x: float, y: float, poly: list) -> bool:
    inside, j = False, len(poly) - 1
    for i in range(len(poly)):
        (xi, yi), (xj, yj) = poly[i], poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def connector_centre(c: dict) -> tuple[float, float]:
    if "u" in c:
        return to_board((c["u"][0] + c["u"][1]) / 2, (c["v"][0] + c["v"][1]) / 2)
    return to_board((c["x"][0] + c["x"][1]) / 2, (c["y"][0] + c["y"][1]) / 2)


def switch_board(side: str, pos: dict, nets: dict, route: bool) -> dict:
    b = Board(side)
    spec = pos["boards"][side]
    b.outline(spec["polygons"])
    parts = nets["boards"][side]
    labels = {"left": {"J1": "FPC20 left", "J2": "SH8 bar display", "J3": "SH5 encoder"},
              "right": {"J1": "FPC20 right", "J2": "SH8 main display"}}[side]
    placed = {}
    for ref, label in labels.items():  # first the connectors, at the places the fit check proved
        c = pos["connectors"][label]
        fp = b.footprint(label.split()[0], ref, parts[ref]["part"].split(",")[0], bottom=True)
        b.face(fp, c["cable_from"])
        b.centre_on(fp, *connector_centre(c))
        b.connect(fp, parts[ref]["pins"])
        placed[ref] = fp
    switch_pads = []
    for s in spec["switches"]:
        x, y = to_board(s["u"], s["v"])
        sw = b.footprint("MX", s["ref"], "MX switch")
        centre = next(p for p in sw.Pads() if p.GetNumber() == "")
        sw.SetPosition(pcbnew.VECTOR2I(mm(x) - centre.GetPosition().x, mm(y) - centre.GetPosition().y))
        node = f"N_R{s['row']}C{s['col']}"
        b.connect(sw, {"1": s["col_net"].split("_")[0], "2": node})
        for p in sw.Pads():
            box = p.GetBoundingBox()
            box.Inflate(mm(0.4))
            switch_pads.append(box)
    for s in spec["switches"]:  # a diode just in front of each switch's centre post, on the underside
        x, y = to_board(s["u"], s["v"])
        node = f"N_R{s['row']}C{s['col']}"
        d = b.footprint("D", s["ref"].replace("SW", "D"), "1N4148W", bottom=True)
        b.connect(d, {"1": s["row_net"].split("_")[0], "2": node})  # pad 1 = cathode -> row (COL2ROW)
        b.place_near(d, x, y + 5.0, switch_pads)
    cap = b.footprint("C0402", "C1", "100nF", bottom=True)
    b.connect(cap, parts["C1"]["pins"])
    j1 = b.courtyard(placed["J1"]).GetCenter()
    b.place_near(cap, pcbnew.ToMM(j1.x), pcbnew.ToMM(j1.y), switch_pads)
    return b.save(route)


def hat(edition: str, pos: dict, nets: dict, route: bool) -> dict:
    b = Board(f"hat-{edition}")
    spec = pos["boards"]["hat"]
    b.outline(spec["polygons"])
    parts = nets["boards"]["hat"]
    pico = b.footprint("PICO", "H1", "Raspberry Pi Pico 2 (below the hat)", bottom=True)
    target = {str(p["pin"]): to_board(p["x"], p["y"]) for p in spec["pico_pins"]}
    for deg in (0, 90, 180, 270):  # the turn that puts every pad on its pin
        pico.SetOrientationDegrees(deg)
        pads = {p.GetNumber(): p.GetPosition() for p in pico.Pads()}
        d = (mm(target["1"][0]) - pads["1"].x, mm(target["1"][1]) - pads["1"].y)
        if all(abs(pads[n].x + d[0] - mm(x)) < mm(0.05) and abs(pads[n].y + d[1] - mm(y)) < mm(0.05)
               for n, (x, y) in target.items()):
            p = pico.GetPosition()
            pico.SetPosition(pcbnew.VECTOR2I(p.x + d[0], p.y + d[1]))
            break
    else:
        raise SystemExit("the Pico footprint's pads do not match the Pico's pins in any orientation")
    b.connect(pico, parts["H1"]["pins"])
    pico_pads = []  # the header pins come through the hat: top-side parts stay off them
    for p in pico.Pads():
        box = p.GetBoundingBox()
        box.Inflate(mm(0.4))
        pico_pads.append(box)
    placed = {}
    labels = {"J1": "FPC20 hat left", "J2": "FPC20 hat right", "J4": "PH3 LED"}
    if edition == "joystick":
        labels["J3"] = "SH5 joystick"
    for ref, label in labels.items():
        c = pos["connectors"][label]
        fp = b.footprint(label.split()[0], ref, parts[ref]["part"].split(",")[0])
        b.face(fp, c["cable_from"])
        b.centre_on(fp, *connector_centre(c))
        b.connect(fp, parts[ref]["pins"])
        placed[ref] = fp
    near = lambda ref: (pcbnew.ToMM(b.courtyard(placed[ref]).GetCenter().x),  # noqa: E731
                        pcbnew.ToMM(b.courtyard(placed[ref]).GetCenter().y))
    smd = [("U1", "SOT", "74AHCT1G125", "J4"), ("R1", "R0402", "330R", "J4"), ("D1", "D", "B5819W", "J4"),
           ("C1", "C0805", "10uF", "J4"), ("C2", "C0402", "100nF", "J4"), ("C3", "C0402", "100nF", "J1"),
           ("C4", "C0402", "100nF", "J2")]
    for ref, kind, value, anchor in smd:
        fp = b.footprint(kind, ref, value)
        b.connect(fp, {k.split()[0]: v for k, v in parts[ref]["pins"].items()} if isinstance(parts[ref]["pins"], dict)
                  else parts[ref]["pins"])
        b.place_near(fp, *near(anchor), pico_pads)
    return b.save(route)


def main() -> int:
    route = "--route" in sys.argv[1:]
    nets = json.loads((PCB / "netlist.json").read_text())
    rotary = json.loads((PCB / "rotary" / "positions.json").read_text())
    joystick = json.loads((PCB / "joystick" / "positions.json").read_text())
    results = {
        "left": switch_board("left", rotary, nets, route),
        "right": switch_board("right", rotary, nets, route),
        "hat-rotary": hat("rotary", rotary, nets, route),
        "hat-joystick": hat("joystick", joystick, nets, route),
    }
    bad = 0
    for name, r in results.items():
        print(f"{name:13s} DRC: {r['violations'] or 'clean'}; unconnected: {r['unconnected']}")
        bad += sum(r["violations"].values()) + r["unconnected"]
    print("wrote", PCB / "kicad")
    return 1 if bad and route else 0


if __name__ == "__main__":
    sys.exit(main())
