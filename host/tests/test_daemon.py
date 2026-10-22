"""The daemon loop against a fake macropad: a SOCK_SEQPACKET pair behaves like hidraw
(one report per read), and the daemon's own HidrawDevice class drives its end."""
import os
import socket
import tempfile
import threading
import unittest

from nyxd import protocol as proto
from nyxd.config import Config
from nyxd.daemon import SCAN_EVERY, Daemon, request
from nyxd.device import HidrawDevice


class FakeCollector:
    def __init__(self):
        self.samples = 0

    def sample(self):
        self.samples += 1
        return proto.Metrics(cpu_pct=42, cores=[1, 2], mem_used_mib=1, mem_total_mib=2, clock=(9, 30))


class FakeMacropad:
    """The device end: receives what the daemon writes (65 bytes, report id 0 first)."""

    def __init__(self):
        self.ours, theirs = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        theirs.setblocking(False)
        self.theirs = theirs
        self.ours.settimeout(1)

    def device(self, path):
        return HidrawDevice(os.dup(self.theirs.fileno()), path)

    def received(self):
        out = []
        self.ours.setblocking(False)
        try:
            while True:
                data = self.ours.recv(100)
                assert len(data) == 65 and data[0] == 0, data
                out.append(data[1:])
        except BlockingIOError:
            pass
        return out

    def kinds(self):
        return [p[0] for p in self.received()]

    def send(self, pkt):
        self.ours.send(pkt)

    def unplug(self):
        self.ours.close()
        self.theirs.close()


def ack():
    return bytes([proto.HELLO_ACK, proto.PROTO_VERSION, 2, 5, 1]) + b"fake fw".ljust(59, b"\0")


class DaemonLoop(unittest.TestCase):
    def setUp(self):
        self.now = 1000.0
        self.pad = FakeMacropad()
        self.spawned = []
        self.found = ["/dev/hidraw7"]
        cfg = Config(interval=1.0, keys={3: "echo hi"}, text={0: "boot text"})
        self.d = Daemon(cfg, FakeCollector(), find=lambda: list(self.found), open_device=self.pad.device,
                        clock=lambda: self.now, spawn=self.spawned.append, hostname="box")

    def tearDown(self):
        self.d.close()

    def step(self, n=3):
        for _ in range(n):
            self.d.step(0)

    def test_session(self):
        self.step()
        first = self.pad.received()
        self.assertEqual([p[0] for p in first], [proto.HELLO, proto.TEXT, proto.METRICS])
        self.assertEqual(first[0][2:5], b"box")
        self.assertEqual(first[1][3:12], b"boot text")

        self.pad.send(ack())
        self.step()
        self.assertEqual(self.d.ack.edition_name, "rotary")
        self.assertEqual(self.pad.kinds(), [])  # nothing more until the next interval

        self.now += 1.0
        self.step()
        self.assertEqual(self.pad.kinds(), [proto.METRICS])

        self.pad.send(bytes([proto.STATE, proto.PROTO_VERSION, 1, 3, 3]).ljust(64, b"\0"))
        self.pad.send(bytes([proto.KEY, proto.PROTO_VERSION, 3, 1]).ljust(64, b"\0"))
        self.pad.send(bytes([proto.KEY, proto.PROTO_VERSION, 3, 0]).ljust(64, b"\0"))  # release: nothing
        self.pad.send(bytes([proto.KEY, proto.PROTO_VERSION, 9, 1]).ljust(64, b"\0"))  # not mapped
        self.step()
        self.assertEqual(self.spawned, ["echo hi"])
        self.assertTrue(self.d.state.host_view)

        reply = self.d.handle({"cmd": "text", "slot": 2, "text": "héllo"})
        self.assertTrue(reply["ok"] and reply["delivered"])
        pkt = self.pad.received()[0]
        self.assertEqual(pkt[:3], bytes([proto.TEXT, proto.PROTO_VERSION, 2]))
        self.assertEqual(pkt[3:8], b"hello")
        self.assertFalse(self.d.handle({"cmd": "text", "slot": 7, "text": "x"})["ok"])

        # the device timed out (e.g. the computer slept) and asks again: replay HELLO and text
        self.pad.send(ack())
        self.step()
        self.assertEqual(self.pad.kinds(), [proto.HELLO, proto.TEXT, proto.TEXT, proto.METRICS])

        self.d.close()
        self.assertEqual(self.pad.kinds(), [proto.BYE])

    def test_reconnects_after_unplug(self):
        self.step()
        self.pad.unplug()
        self.step()
        self.assertIsNone(self.d.dev)
        self.pad = FakeMacropad()
        self.d.open_device = self.pad.device
        self.step()
        self.assertIsNone(self.d.dev)  # waits SCAN_EVERY between attempts
        self.now += SCAN_EVERY
        self.step()
        self.assertIsNotNone(self.d.dev)
        self.assertEqual(self.pad.kinds()[0], proto.HELLO)

    def test_no_device_found(self):
        self.found = []
        self.step()
        self.assertIsNone(self.d.dev)
        self.assertEqual(self.d.handle({"cmd": "status"})["connected"], False)
        self.assertEqual(self.d.handle({"cmd": "text", "slot": 1, "text": "later"})["delivered"], False)
        self.found = ["/dev/hidraw7"]
        self.now += SCAN_EVERY
        self.step()
        kinds = self.pad.kinds()
        self.assertEqual(kinds.count(proto.TEXT), 2)  # both slots set so far are sent on connect


class ControlSocket(unittest.TestCase):
    def test_nyxctl_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "nyxd.sock")
            d = Daemon(Config(), FakeCollector(), find=lambda: [], sock_path=path)
            self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
            replies = []
            t = threading.Thread(target=lambda: replies.extend([
                request({"cmd": "text", "slot": 1, "text": "from nyxctl"}, path),
                request({"cmd": "status"}, path),
                request({"cmd": "bogus"}, path)]))
            t.start()
            while t.is_alive():
                d.step(0.05)
            t.join()
            d.close()
            self.assertTrue(replies[0]["ok"])
            self.assertEqual(replies[1]["text"][1], "from nyxctl")
            self.assertFalse(replies[2]["ok"])
            self.assertFalse(os.path.exists(path))


if __name__ == "__main__":
    unittest.main()
