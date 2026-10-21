"""Command-line entry points: `nyxd` (the daemon) and `nyxctl` (talks to it)."""
from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from . import __version__
from . import config as config_mod
from .daemon import Daemon, control_path, request
from .device import find_devices
from .metrics import Collector


def nyxd(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="nyxd", description="Nyxilab macropad host bridge: shows this "
                                 "computer's CPU, memory, ... on the macropad's HOST layer.")
    ap.add_argument("--config", type=Path, help=f"default: {config_mod.default_path()}")
    ap.add_argument("--device", help="use this /dev/hidrawN instead of searching")
    ap.add_argument("--interval", type=float, help="seconds between updates (0.2 .. 4)")
    ap.add_argument("--socket", default=control_path(), help="control socket for nyxctl (default: %(default)s)")
    ap.add_argument("--probe", action="store_true", help="list macropads and print one metrics sample, then exit")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--version", action="version", version=f"nyxd {__version__}")
    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format="nyxd: %(message)s")

    cfg = config_mod.load(args.config)
    if args.device:
        cfg.device = args.device
    if args.interval:
        cfg.interval = args.interval
    collector = Collector(disk_path=cfg.disk_path, gpu=cfg.gpu, net_exclude=cfg.net_exclude)

    if args.probe:
        import time
        time.sleep(1)  # a CPU / network rate needs two samples
        print("macropads:", ", ".join(find_devices()) or "none found")
        print(json.dumps(collector.sample().__dict__, indent=2, default=str))
        return 0
    Daemon(cfg, collector, sock_path=args.socket).run()
    return 0


def nyxctl(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="nyxctl", description="Control a running nyxd.")
    ap.add_argument("--socket", default=control_path())
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status", help="connection state and current text")
    t = sub.add_parser("text", help="show text in a slot on the bar display (until replaced)")
    t.add_argument("slot", type=int, help="0..3")
    t.add_argument("text", nargs="+")
    c = sub.add_parser("clear", help="clear one text slot, or all")
    c.add_argument("slot", type=int, nargs="?")
    args = ap.parse_args(argv)

    req = {"cmd": args.cmd}
    if args.cmd == "text":
        req.update(slot=args.slot, text=" ".join(args.text))
    elif args.cmd == "clear":
        req["slot"] = args.slot
    try:
        reply = request(req, args.socket)
    except OSError as e:
        print(f"nyxctl: cannot reach nyxd at {args.socket}: {e.strerror or e}", file=sys.stderr)
        return 2
    if not reply.get("ok"):
        print(f"nyxctl: {reply.get('error')}", file=sys.stderr)
        return 1
    if args.cmd == "status":
        for key, value in reply.items():
            if key != "ok":
                print(f"{key:18} {value}")
    return 0


if __name__ == "__main__":
    sys.exit(nyxd())
