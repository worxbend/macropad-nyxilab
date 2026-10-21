"""The collector against a fake /proc and /sys."""
import tempfile
import time
import unittest
from pathlib import Path

from nyxd.metrics import Collector

STAT_1 = """cpu  100 0 100 800 0 0 0 0 0 0
cpu0 50 0 50 400 0 0 0 0 0 0
cpu1 50 0 50 400 0 0 0 0 0 0
intr 1 2 3
"""
# +100 busy / +200 total overall: cpu0 fully busy, cpu1 idle
STAT_2 = """cpu  200 0 100 900 0 0 0 0 0 0
cpu0 150 0 50 400 0 0 0 0 0 0
cpu1 50 0 50 500 0 0 0 0 0 0
"""
MEMINFO = """MemTotal:       16384000 kB
MemFree:         1000000 kB
MemAvailable:    8192000 kB
SwapTotal:       2000000 kB
SwapFree:        1500000 kB
"""
NETDEV_1 = """Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
    lo: 999999 0 0 0 0 0 0 0 999999 0 0 0 0 0 0 0
  eth0: 1000 0 0 0 0 0 0 0 500 0 0 0 0 0 0 0
docker0: 777777 0 0 0 0 0 0 0 777777 0 0 0 0 0 0 0
"""
NETDEV_2 = NETDEV_1.replace("eth0: 1000", "eth0: 5000").replace("0 500 0", "0 2500 0")


class Collect(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.proc, self.sys = root / "proc", root / "sys"
        (self.proc / "net").mkdir(parents=True)
        self.write("proc/stat", STAT_1)
        self.write("proc/meminfo", MEMINFO)
        self.write("proc/loadavg", "1.23 0.50 0.25 1/100 999\n")
        self.write("proc/uptime", "93784.12 1000.00\n")
        self.write("proc/net/dev", NETDEV_1)
        self.write("sys/class/hwmon/hwmon0/name", "nvme\n")
        self.write("sys/class/hwmon/hwmon0/temp1_input", "38000\n")
        self.write("sys/class/hwmon/hwmon3/name", "k10temp\n")
        self.write("sys/class/hwmon/hwmon3/temp1_input", "54660\n")
        self.write("sys/class/drm/card1/device/gpu_busy_percent", "12\n")
        self.write("sys/class/drm/card1/device/hwmon/hwmon5/temp1_input", "46000\n")
        self.now = 100.0

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, rel, text):
        p = Path(self.tmp.name) / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)

    def collector(self, **kw):
        lt = time.struct_time((2026, 10, 21, 14, 7, 0, 2, 294, -1))
        return Collector(proc=str(self.proc), sys=str(self.sys), disk_path=self.tmp.name, gpu="amd",
                         clock=lambda: self.now, localtime=lambda: lt, **kw)

    def test_rates_and_values(self):
        c = self.collector()
        self.write("proc/stat", STAT_2)
        self.write("proc/net/dev", NETDEV_2)
        self.now += 2.0
        m = c.sample()
        self.assertAlmostEqual(m.cpu_pct, 50.0)
        self.assertEqual([round(x) for x in m.cores], [100, 0])
        self.assertEqual((m.mem_used_mib, m.mem_total_mib), (8000, 16000))
        self.assertAlmostEqual(m.swap_pct, 25.0)
        self.assertEqual(m.load1, 1.23)
        self.assertEqual(int(m.uptime_s), 93784)
        self.assertEqual((m.net_rx_bps, m.net_tx_bps), (2000.0, 1000.0))  # lo and docker0 left out
        self.assertEqual(m.cpu_temp_c, 54.66)  # k10temp preferred over the NVMe sensor
        self.assertEqual((m.gpu_pct, m.gpu_temp_c), (12.0, 46.0))
        self.assertEqual(m.clock, (14, 7))
        self.assertTrue(0 <= m.disk_pct <= 100)

    def test_first_sample_has_no_cpu_rate_and_missing_files_are_unknown(self):
        for rel in ("proc/meminfo", "proc/loadavg"):
            (Path(self.tmp.name) / rel).unlink()
        c = Collector(proc=str(self.proc), sys=str(Path(self.tmp.name) / "nosys"), gpu="off",
                      clock=lambda: self.now)
        m = c.sample()  # same counters as the priming sample: no time has passed
        self.assertIsNone(m.cpu_pct)
        self.assertEqual(m.mem_total_mib, 0)
        self.assertIsNone(m.swap_pct)
        self.assertIsNone(m.cpu_temp_c)
        self.assertIsNone(m.gpu_pct)

    def test_real_system(self):
        m = Collector().sample()  # whatever this machine has: must not raise
        self.assertGreater(m.mem_total_mib, 0)


if __name__ == "__main__":
    unittest.main()
