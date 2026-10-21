"""The daemon's encoder against the firmware's real parser (tests/peer.cpp + core/host_proto.cpp)."""
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from nyxd import protocol as proto

REPO = Path(__file__).resolve().parents[2]
CORE = REPO / "common" / "firmware" / "lib" / "nyx_core" / "src"


@unittest.skipUnless(shutil.which("g++"), "needs g++")
class Interop(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "peer"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", f"-I{CORE}",
                        str(Path(__file__).with_name("peer.cpp")), str(CORE / "core" / "host_proto.cpp"),
                        "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_round_trip(self):
        m = proto.Metrics(cpu_pct=37.4, cores=[10, 20.6, 30, 100] + [50] * 16, mem_used_mib=11674,
                          mem_total_mib=31890, swap_pct=3, disk_pct=61.2, cpu_temp_c=54.66, load1=2.14,
                          net_rx_bps=1_840_000.7, net_tx_bps=96_300, uptime_s=266520.9, gpu_pct=None,
                          gpu_temp_c=46.1, clock=(14, 7))
        packets = [proto.hello("désk-01"), proto.metrics(m), proto.text(2, "build ✓ ok")]
        run = subprocess.run([str(self.exe)], input=b"".join(packets), capture_output=True, check=True)

        replies = [proto.decode(run.stdout[i:i + 64]) for i in range(0, len(run.stdout), 64)]
        self.assertEqual(replies, [proto.HelloAck(2, 5, proto.PROTO_VERSION, "peer firmware"),
                                   proto.State(True, 3, 3), proto.Key(5, True)])

        seen = json.loads(run.stderr)
        self.assertEqual(seen["linked"], 1)
        self.assertEqual(seen["hostname"], "desk-01")  # accent stripped by the daemon
        self.assertEqual(seen["cpu"], 37)
        self.assertEqual(len(seen["cores"]), 16)  # 20 cores averaged into 16 bars
        self.assertEqual(seen["cores"][:3], [10, 21, 30])
        self.assertEqual((seen["mem_used"], seen["mem_total"]), (11674, 31890))
        self.assertEqual((seen["swap"], seen["disk"]), (3, 61))
        self.assertEqual((seen["temp10"], seen["load100"]), (547, 214))
        self.assertEqual((seen["rx"], seen["tx"], seen["up"]), (1840000, 96300, 266520))
        self.assertEqual((seen["gpu"], seen["gpu_temp10"]), (255, 461))
        self.assertEqual((seen["h"], seen["m"]), (14, 7))
        self.assertEqual(seen["text"], ["", "", "build ? ok", ""])


if __name__ == "__main__":
    unittest.main()
