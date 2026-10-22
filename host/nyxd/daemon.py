"""The bridge: finds the macropad, feeds it metrics and text, runs commands for its HOST keys.

One thread, one selector: the device's hidraw node, the control socket that nyxctl
talks to, and two timers (metrics, device discovery).  Unplugging and replugging
the macropad, or restarting it, needs no action: the daemon reconnects.
"""
from __future__ import annotations

import json
import logging
import os
import selectors
import signal
import socket
import subprocess
import time
from pathlib import Path
from typing import Callable, Optional

from . import protocol as proto
from .config import Config
from .device import HidrawDevice, find_devices
from .metrics import Collector

log = logging.getLogger("nyxd")

SCAN_EVERY = 2.0  # seconds between looks for a (re)connected macropad
ACK_WAIT = 3.0    # warn if HELLO is not answered by then (firmware without the host link)


def control_path() -> str:
    run = os.environ.get("XDG_RUNTIME_DIR")
    return os.path.join(run, "nyxd.sock") if run else f"/tmp/nyxd-{os.getuid()}.sock"


class Daemon:
    def __init__(self, cfg: Config, collector: Collector, *,
                 find: Callable[[], list[str]] = find_devices,
                 open_device: Callable[[str], HidrawDevice] = HidrawDevice.open,
                 clock: Callable[[], float] = time.monotonic,
                 spawn: Callable[[str], object] = lambda cmd: subprocess.Popen(
                     cmd, shell=True, start_new_session=True, stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL),
                 hostname: Optional[str] = None, sock_path: Optional[str] = None):
        self.cfg, self.collector = cfg, collector
        self.find, self.open_device, self.clock, self.spawn = find, open_device, clock, spawn
        self.hostname = hostname or socket.gethostname()
        self.sel = selectors.DefaultSelector()
        self.dev: Optional[HidrawDevice] = None
        self.ack: Optional[proto.HelloAck] = None
        self.state: Optional[proto.State] = None
        self.texts = ["" for _ in range(proto.TEXT_SLOTS)]
        for slot, value in cfg.text.items():
            if 0 <= slot < proto.TEXT_SLOTS:
                self.texts[slot] = value
        self.next_scan = 0.0
        self.next_metrics = 0.0
        self.hello_at: Optional[float] = None
        self.sent = 0
        self.stopping = False
        self.waiting_logged = False
        self.sock_path = sock_path
        self.server: Optional[socket.socket] = None
        if sock_path:
            self._listen(sock_path)

    # ------------------------------------------------------------- device
    def _connect(self, now: float) -> None:
        self.next_scan = now + SCAN_EVERY
        paths = [self.cfg.device] if self.cfg.device else self.find()
        if not paths and not self.waiting_logged:
            log.info("no macropad yet; waiting for one with the host-link firmware to be plugged in")
            self.waiting_logged = True
        for path in paths:
            try:
                dev = self.open_device(path)
            except OSError as e:
                log.warning("cannot open %s: %s (is the udev rule installed?)", path, e.strerror)
                continue
            self.dev, self.ack, self.state = dev, None, None
            self.sel.register(dev, selectors.EVENT_READ, self._on_device)
            log.info("macropad found at %s", path)
            self._hello(now)
            return

    def _hello(self, now: float) -> None:
        """Start a session: HELLO, then everything the device should show besides metrics."""
        self.hello_at = now
        if self._send(proto.hello(self.hostname)):
            for slot, value in enumerate(self.texts):
                if value:
                    self._send(proto.text(slot, value))
        self.next_metrics = now  # the first update right away

    def _disconnect(self, why: str) -> None:
        if self.dev:
            log.info("macropad disconnected (%s)", why)
            self.sel.unregister(self.dev)
            self.dev.close()
        self.dev, self.ack, self.state, self.hello_at = None, None, None, None
        self.next_scan = self.clock() + SCAN_EVERY

    def _send(self, pkt: bytes) -> bool:
        if not self.dev:
            return False
        try:
            ok = self.dev.write(pkt)
        except OSError as e:
            self._disconnect(e.strerror or str(e))
            return False
        if ok:
            self.sent += 1
        return ok

    def _on_device(self) -> None:
        while self.dev:
            try:
                pkt = self.dev.read()
            except OSError as e:
                self._disconnect(e.strerror or str(e))
                return
            if not pkt:
                if pkt == b"":  # EOF: the node went away
                    self._disconnect("end of file")
                return
            self._on_event(proto.decode(pkt))

    def _on_event(self, ev: Optional[proto.Event]) -> None:
        if isinstance(ev, proto.HelloAck):
            if self.hello_at is None:  # unrequested: the device lost our session (link timeout)
                log.info("the macropad restarted its session; sending everything again")
                self._hello(self.clock())
                return
            self.ack, self.hello_at = ev, None
            log.info("connected: %s, %s edition, %d layers, protocol %d",
                     ev.firmware, ev.edition_name, ev.n_layers, ev.proto)
        elif isinstance(ev, proto.State):
            if ev != self.state:
                log.debug("dashboard %s (base layer %d, top %d)", "shown" if ev.host_view else "hidden",
                          ev.base, ev.top)
            self.state = ev
        elif isinstance(ev, proto.Key):
            self._on_key(ev)

    def _on_key(self, ev: proto.Key) -> None:
        if not ev.pressed:
            return
        cmd = self.cfg.keys.get(ev.id)
        if not cmd:
            log.info("HOST(%d) pressed; map it to a command in [keys] of the config", ev.id)
            return
        log.info("HOST(%d): %s", ev.id, cmd)
        try:
            self.spawn(cmd)
        except OSError as e:
            log.error("HOST(%d): cannot run %r: %s", ev.id, cmd, e)

    # ------------------------------------------------------------ control
    def _listen(self, path: str) -> None:
        if os.path.exists(path):
            probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                probe.connect(path)
                raise SystemExit(f"nyxd is already running ({path})")
            except (ConnectionRefusedError, FileNotFoundError):
                os.unlink(path)  # left over from a crash
            finally:
                probe.close()
        old = os.umask(0o177)  # owner only: anyone who can connect can change the display
        try:
            self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.server.bind(path)
        finally:
            os.umask(old)
        self.server.listen(4)
        self.server.setblocking(False)
        self.sel.register(self.server, selectors.EVENT_READ, self._on_client)

    def _on_client(self) -> None:
        assert self.server
        try:
            conn, _ = self.server.accept()
        except BlockingIOError:
            return
        with conn:
            conn.settimeout(1.0)
            try:
                data = b""
                while not data.endswith(b"\n") and len(data) < 4096:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    data += chunk
                reply = self.handle(json.loads(data or b"{}"))
            except (OSError, ValueError) as e:
                reply = {"ok": False, "error": str(e)}
            try:
                conn.sendall(json.dumps(reply).encode() + b"\n")
            except OSError:
                pass

    def handle(self, req: dict) -> dict:
        """One nyxctl request: {"cmd": "status" | "text" | "clear", ...}."""
        cmd = req.get("cmd")
        if cmd == "status":
            return {"ok": True, "connected": self.dev is not None, "device": self.dev.path if self.dev else None,
                    "firmware": self.ack.firmware if self.ack else None,
                    "edition": self.ack.edition_name if self.ack else None,
                    "dashboard_visible": self.state.host_view if self.state else None,
                    "packets_sent": self.sent, "text": self.texts}
        if cmd == "text":
            slot = int(req.get("slot", -1))
            if not 0 <= slot < proto.TEXT_SLOTS:
                return {"ok": False, "error": f"slot must be 0..{proto.TEXT_SLOTS - 1}"}
            self.texts[slot] = str(req.get("text", ""))
            self._send(proto.text(slot, self.texts[slot]))
            return {"ok": True, "delivered": self.dev is not None}
        if cmd == "clear":
            slots = range(proto.TEXT_SLOTS) if req.get("slot") is None else [int(req["slot"])]
            for slot in slots:
                if 0 <= slot < proto.TEXT_SLOTS:
                    self.texts[slot] = ""
                    self._send(proto.text(slot, ""))
            return {"ok": True}
        return {"ok": False, "error": f"unknown command {cmd!r}"}

    # --------------------------------------------------------------- loop
    def step(self, max_wait: float = 0.5) -> None:
        now = self.clock()
        if not self.dev and now >= self.next_scan:
            self._connect(now)
        if self.dev and now >= self.next_metrics:
            self.next_metrics = now + self.cfg.interval
            self._send(proto.metrics(self.collector.sample()))
        if self.dev and self.hello_at is not None and now - self.hello_at > ACK_WAIT:
            log.warning("the macropad does not answer; its firmware may predate the host link")
            self.hello_at = None
        deadline = self.next_metrics if self.dev else self.next_scan
        for key, _ in self.sel.select(max(0.0, min(max_wait, deadline - now))):
            key.data()

    def run(self) -> None:
        def stop(*_):
            self.stopping = True

        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        log.info("nyxd started; control socket %s", self.sock_path or "off")
        while not self.stopping:
            self.step()
        self.close()

    def close(self) -> None:
        if self.dev:
            self._send(proto.bye())  # the dashboard switches to "waiting for host" at once
            self._disconnect("stopping")
        if self.server:
            self.sel.unregister(self.server)
            self.server.close()
            if self.sock_path:
                try:
                    os.unlink(self.sock_path)
                except OSError:
                    pass
            self.server = None


def request(req: dict, path: Optional[str] = None, timeout: float = 2.0) -> dict:
    """Send one request to a running daemon (what nyxctl does)."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(timeout)
        s.connect(path or control_path())
        s.sendall(json.dumps(req).encode() + b"\n")
        data = b""
        while not data.endswith(b"\n"):
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    return json.loads(data)
