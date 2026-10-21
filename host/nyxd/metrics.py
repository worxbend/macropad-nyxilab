"""Linux host metrics from /proc and /sys (no dependencies).

CPU and network figures are rates, so the collector keeps the previous counters;
the first sample after start-up has no CPU figure.  `proc` / `sys` can point at a
fixture tree for tests.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import time
from pathlib import Path
from typing import Callable, Optional, Sequence

from .protocol import Metrics

# virtual interfaces would count container / VM traffic twice
NET_EXCLUDE = ("lo", "veth", "docker", "br-", "virbr", "cni", "flannel", "vnet")
CPU_SENSORS = ("k10temp", "coretemp", "zenpower", "cpu_thermal", "soc_thermal", "acpitz")


def _read(path: Path) -> Optional[str]:
    try:
        return path.read_text()
    except OSError:
        return None


def _cpu_times(stat: str) -> dict[str, tuple[int, int]]:
    """{"cpu": (busy, total), "cpu0": ...} from /proc/stat."""
    out = {}
    for line in stat.splitlines():
        if not line.startswith("cpu"):
            continue
        name, *vals = line.split()
        v = [int(x) for x in vals[:8]] + [0] * (8 - len(vals[:8]))
        idle = v[3] + v[4]  # idle + iowait
        total = sum(v)  # guest time is already part of user / nice
        out[name] = (total - idle, total)
    return out


def _busy_pct(prev: tuple[int, int], cur: tuple[int, int]) -> Optional[float]:
    d_total = cur[1] - prev[1]
    if d_total <= 0:
        return None
    return max(0.0, min(100.0, 100.0 * (cur[0] - prev[0]) / d_total))


def _meminfo(text: str) -> dict[str, int]:
    out = {}
    for line in text.splitlines():
        key, _, rest = line.partition(":")
        parts = rest.split()
        if parts:
            out[key] = int(parts[0])  # kB
    return out


def _net_bytes(text: str, exclude: Sequence[str]) -> tuple[int, int]:
    rx = tx = 0
    for line in text.splitlines()[2:]:
        name, _, rest = line.partition(":")
        name = name.strip()
        if name == "lo" or any(name.startswith(p) for p in exclude):
            continue
        f = rest.split()
        if len(f) >= 9:
            rx += int(f[0])
            tx += int(f[8])
    return rx, tx


class Collector:
    def __init__(self, proc: str = "/proc", sys: str = "/sys", disk_path: str = "/", gpu: str = "auto",
                 net_exclude: Sequence[str] = NET_EXCLUDE, clock: Callable[[], float] = time.monotonic,
                 localtime: Callable[[], time.struct_time] = time.localtime):
        self.proc, self.sys = Path(proc), Path(sys)
        self.disk_path, self.gpu, self.net_exclude = disk_path, gpu, tuple(net_exclude)
        self.clock, self.localtime = clock, localtime
        self._cpu: dict[str, tuple[int, int]] = {}
        self._net: Optional[tuple[float, int, int]] = None
        self._cpu_temp_file = self._find_cpu_sensor()
        self._amd_gpu = self._find_amd_gpu()
        self._nvidia = shutil.which("nvidia-smi") if gpu in ("auto", "nvidia") else None
        self.sample()  # prime the rates

    # ------------------------------------------------------------ discovery
    def _find_cpu_sensor(self) -> Optional[Path]:
        found = {}
        for d in sorted((self.sys / "class" / "hwmon").glob("hwmon*")):
            name = (_read(d / "name") or "").strip()
            if name in CPU_SENSORS and (d / "temp1_input").exists():
                found.setdefault(name, d / "temp1_input")
        for name in CPU_SENSORS:
            if name in found:
                return found[name]
        for z in sorted((self.sys / "class" / "thermal").glob("thermal_zone*")):  # e.g. x86_pkg_temp
            if (_read(z / "type") or "").strip() in ("x86_pkg_temp", "cpu-thermal", "cpu_thermal"):
                return z / "temp"
        return None

    def _find_amd_gpu(self) -> Optional[tuple[Path, Optional[Path]]]:
        if self.gpu not in ("auto", "amd"):
            return None
        for card in sorted((self.sys / "class" / "drm").glob("card[0-9]")):
            busy = card / "device" / "gpu_busy_percent"
            if busy.exists():
                temps = sorted((card / "device" / "hwmon").glob("hwmon*/temp1_input"))
                return busy, temps[0] if temps else None
        return None

    # -------------------------------------------------------------- sample
    def sample(self) -> Metrics:
        m = Metrics()
        now = self.clock()
        stat = _read(self.proc / "stat")
        if stat:
            cur = _cpu_times(stat)
            if "cpu" in self._cpu and "cpu" in cur:
                m.cpu_pct = _busy_pct(self._cpu["cpu"], cur["cpu"])
                names = sorted((k for k in cur if k != "cpu"), key=lambda k: int(k[3:]))
                m.cores = [(_busy_pct(self._cpu[k], cur[k]) or 0.0) if k in self._cpu else 0.0 for k in names]
            self._cpu = cur

        mem = _meminfo(_read(self.proc / "meminfo") or "")
        if "MemTotal" in mem:
            avail = mem.get("MemAvailable", mem.get("MemFree", 0) + mem.get("Cached", 0))
            m.mem_total_mib = mem["MemTotal"] // 1024
            m.mem_used_mib = max(0, mem["MemTotal"] - avail) // 1024
        if mem.get("SwapTotal"):
            m.swap_pct = 100.0 * (mem["SwapTotal"] - mem.get("SwapFree", 0)) / mem["SwapTotal"]

        try:
            st = os.statvfs(self.disk_path)
            if st.f_blocks:
                used = st.f_blocks - st.f_bfree
                m.disk_pct = 100.0 * used / (used + st.f_bavail)  # as df reports it
        except OSError:
            pass

        load = (_read(self.proc / "loadavg") or "").split()
        if load:
            m.load1 = float(load[0])
        up = (_read(self.proc / "uptime") or "").split()
        if up:
            m.uptime_s = float(up[0])

        net = _read(self.proc / "net" / "dev")
        if net:
            rx, tx = _net_bytes(net, self.net_exclude)
            if self._net and now > self._net[0]:
                dt = now - self._net[0]
                m.net_rx_bps = max(0, rx - self._net[1]) / dt
                m.net_tx_bps = max(0, tx - self._net[2]) / dt
            self._net = (now, rx, tx)

        if self._cpu_temp_file:
            t = (_read(self._cpu_temp_file) or "").strip()
            m.cpu_temp_c = int(t) / 1000 if t.lstrip("-").isdigit() else None
        self._gpu(m)

        lt = self.localtime()
        m.clock = (lt.tm_hour, lt.tm_min)
        return m

    def _gpu(self, m: Metrics) -> None:
        if self._amd_gpu:
            busy, temp = self._amd_gpu
            b = (_read(busy) or "").strip()
            m.gpu_pct = float(b) if b.isdigit() else None
            t = (_read(temp) or "").strip() if temp else ""
            m.gpu_temp_c = int(t) / 1000 if t.isdigit() else None
        elif self._nvidia:
            try:
                out = subprocess.run([self._nvidia, "--query-gpu=utilization.gpu,temperature.gpu",
                                      "--format=csv,noheader,nounits"], capture_output=True, text=True,
                                     timeout=2).stdout.splitlines()
                util, temp = (x.strip() for x in out[0].split(","))
                m.gpu_pct, m.gpu_temp_c = float(util), float(temp)
            except (OSError, subprocess.SubprocessError, ValueError, IndexError):
                pass
