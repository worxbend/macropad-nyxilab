//! Linux host metrics from /proc and /sys.
//!
//! CPU and network figures are rates, so the collector keeps the previous counters;
//! the first sample after start-up has no CPU figure.  The roots can point at a
//! fixture tree for tests.

use std::collections::HashMap;
use std::ffi::CString;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

use crate::protocol::Metrics;

/// Interface name prefixes left out of the network figures (virtual links count traffic twice).
pub const NET_EXCLUDE: &[&str] = &["lo", "veth", "docker", "br-", "virbr", "cni", "flannel", "vnet"];
const CPU_SENSORS: &[&str] = &["k10temp", "coretemp", "zenpower", "cpu_thermal", "soc_thermal", "acpitz"];

/// Something that produces metrics (the collector, or a fake in tests).
pub trait Source {
    fn sample(&mut self) -> Metrics;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Gpu {
    Auto,
    Amd,
    Nvidia,
    Off,
}

pub struct Collector {
    proc_root: PathBuf,
    disk_path: PathBuf,
    net_exclude: Vec<String>,
    clock: Box<dyn Fn() -> f64>,
    localtime: Box<dyn Fn() -> Option<(u8, u8)>>,
    cpu: HashMap<String, (u64, u64)>,
    net: Option<(f64, u64, u64)>,
    cpu_temp_file: Option<PathBuf>,
    amd_gpu: Option<(PathBuf, Option<PathBuf>)>,
    nvidia: Option<PathBuf>,
}

fn read(path: &Path) -> Option<String> {
    fs::read_to_string(path).ok()
}

fn sorted_glob(dir: &Path, prefix: &str) -> Vec<PathBuf> {
    let mut out: Vec<PathBuf> = fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.file_name().and_then(|n| n.to_str()).is_some_and(|n| n.starts_with(prefix)))
        .collect();
    out.sort();
    out
}

/// {"cpu": (busy, total), "cpu0": ...} from /proc/stat.
fn cpu_times(stat: &str) -> HashMap<String, (u64, u64)> {
    let mut out = HashMap::new();
    for line in stat.lines().filter(|l| l.starts_with("cpu")) {
        let mut f = line.split_whitespace();
        let name = f.next().unwrap_or_default().to_string();
        let mut v: Vec<u64> = f.take(8).map(|x| x.parse().unwrap_or(0)).collect();
        v.resize(8, 0);
        let idle = v[3] + v[4]; // idle + iowait
        let total: u64 = v.iter().sum(); // guest time is already part of user / nice
        out.insert(name, (total - idle, total));
    }
    out
}

fn busy_pct(prev: (u64, u64), cur: (u64, u64)) -> Option<f64> {
    let d_total = cur.1.checked_sub(prev.1)?;
    if d_total == 0 {
        return None;
    }
    let d_busy = cur.0.saturating_sub(prev.0);
    Some((100.0 * d_busy as f64 / d_total as f64).clamp(0.0, 100.0))
}

fn meminfo(text: &str) -> HashMap<String, u64> {
    text.lines()
        .filter_map(|l| {
            let (k, rest) = l.split_once(':')?;
            Some((k.to_string(), rest.split_whitespace().next()?.parse().ok()?))
            // kB
        })
        .collect()
}

fn net_bytes(text: &str, exclude: &[String]) -> (u64, u64) {
    let (mut rx, mut tx) = (0, 0);
    for line in text.lines().skip(2) {
        let Some((name, rest)) = line.split_once(':') else { continue };
        let name = name.trim();
        if name == "lo" || exclude.iter().any(|p| name.starts_with(p.as_str())) {
            continue;
        }
        let f: Vec<u64> = rest.split_whitespace().map(|x| x.parse().unwrap_or(0)).collect();
        if f.len() >= 9 {
            rx += f[0];
            tx += f[8];
        }
    }
    (rx, tx)
}

fn milli_degrees(path: &Path) -> Option<f64> {
    read(path)?.trim().parse::<i64>().ok().map(|v| v as f64 / 1000.0)
}

fn which(program: &str) -> Option<PathBuf> {
    std::env::var_os("PATH")?.to_str()?.split(':').map(|d| Path::new(d).join(program)).find(|p| p.is_file())
}

/// The local wall-clock hour and minute.
pub fn local_clock() -> Option<(u8, u8)> {
    let now = unsafe { libc::time(std::ptr::null_mut()) };
    let mut tm: libc::tm = unsafe { std::mem::zeroed() };
    if unsafe { libc::localtime_r(&now, &mut tm) }.is_null() {
        return None;
    }
    Some((tm.tm_hour as u8, tm.tm_min as u8))
}

/// Used share of a file system, as `df` reports it.
fn disk_pct(path: &Path) -> Option<f64> {
    let c = CString::new(path.as_os_str().as_encoded_bytes()).ok()?;
    let mut st: libc::statvfs = unsafe { std::mem::zeroed() };
    if unsafe { libc::statvfs(c.as_ptr(), &mut st) } != 0 || st.f_blocks == 0 {
        return None;
    }
    let used = (st.f_blocks - st.f_bfree) as f64;
    Some(100.0 * used / (used + st.f_bavail as f64))
}

pub struct Options {
    pub proc_root: PathBuf,
    pub sys_root: PathBuf,
    pub disk_path: PathBuf,
    pub gpu: Gpu,
    pub net_exclude: Vec<String>,
}

impl Default for Options {
    fn default() -> Self {
        Options {
            proc_root: "/proc".into(),
            sys_root: "/sys".into(),
            disk_path: "/".into(),
            gpu: Gpu::Auto,
            net_exclude: NET_EXCLUDE.iter().map(|s| s.to_string()).collect(),
        }
    }
}

impl Collector {
    pub fn new(opts: Options) -> Self {
        let start = Instant::now();
        Self::with_clock(opts, Box::new(move || start.elapsed().as_secs_f64()), Box::new(local_clock))
    }

    pub fn with_clock(
        opts: Options,
        clock: Box<dyn Fn() -> f64>,
        localtime: Box<dyn Fn() -> Option<(u8, u8)>>,
    ) -> Self {
        let mut c = Collector {
            cpu_temp_file: find_cpu_sensor(&opts.sys_root),
            amd_gpu: if matches!(opts.gpu, Gpu::Auto | Gpu::Amd) { find_amd_gpu(&opts.sys_root) } else { None },
            nvidia: if matches!(opts.gpu, Gpu::Auto | Gpu::Nvidia) { which("nvidia-smi") } else { None },
            proc_root: opts.proc_root,
            disk_path: opts.disk_path,
            net_exclude: opts.net_exclude,
            clock,
            localtime,
            cpu: HashMap::new(),
            net: None,
        };
        c.sample(); // prime the rates
        c
    }

    fn gpu(&self, m: &mut Metrics) {
        if let Some((busy, temp)) = &self.amd_gpu {
            m.gpu_pct = read(busy).and_then(|b| b.trim().parse().ok());
            m.gpu_temp_c = temp.as_deref().and_then(milli_degrees);
        } else if let Some(smi) = &self.nvidia {
            let out = Command::new(smi)
                .args(["--query-gpu=utilization.gpu,temperature.gpu", "--format=csv,noheader,nounits"])
                .output();
            if let Ok(out) = out {
                let text = String::from_utf8_lossy(&out.stdout);
                if let Some((u, t)) = text.lines().next().and_then(|l| l.split_once(',')) {
                    m.gpu_pct = u.trim().parse().ok();
                    m.gpu_temp_c = t.trim().parse().ok();
                }
            }
        }
    }
}

fn find_cpu_sensor(sys: &Path) -> Option<PathBuf> {
    let mut found: HashMap<String, PathBuf> = HashMap::new();
    for d in sorted_glob(&sys.join("class/hwmon"), "hwmon") {
        let name = read(&d.join("name")).unwrap_or_default().trim().to_string();
        if CPU_SENSORS.contains(&name.as_str()) && d.join("temp1_input").exists() {
            found.entry(name).or_insert(d.join("temp1_input"));
        }
    }
    if let Some(p) = CPU_SENSORS.iter().find_map(|n| found.remove(*n)) {
        return Some(p);
    }
    sorted_glob(&sys.join("class/thermal"), "thermal_zone").into_iter().find_map(|z| {
        let t = read(&z.join("type")).unwrap_or_default();
        ["x86_pkg_temp", "cpu-thermal", "cpu_thermal"].contains(&t.trim()).then(|| z.join("temp"))
    })
}

fn find_amd_gpu(sys: &Path) -> Option<(PathBuf, Option<PathBuf>)> {
    sorted_glob(&sys.join("class/drm"), "card").into_iter().find_map(|card| {
        let name = card.file_name()?.to_str()?;
        if !name[4..].chars().all(|c| c.is_ascii_digit()) || name.len() == 4 {
            return None; // card0, not card0-DP-1
        }
        let busy = card.join("device/gpu_busy_percent");
        if !busy.exists() {
            return None;
        }
        let temp = sorted_glob(&card.join("device/hwmon"), "hwmon")
            .into_iter()
            .map(|h| h.join("temp1_input"))
            .find(|p| p.exists());
        Some((busy, temp))
    })
}

impl Source for Collector {
    fn sample(&mut self) -> Metrics {
        let mut m = Metrics::default();
        let now = (self.clock)();

        if let Some(stat) = read(&self.proc_root.join("stat")) {
            let cur = cpu_times(&stat);
            if let (Some(p), Some(c)) = (self.cpu.get("cpu"), cur.get("cpu")) {
                m.cpu_pct = busy_pct(*p, *c);
                let mut names: Vec<&String> = cur.keys().filter(|k| k.as_str() != "cpu").collect();
                names.sort_by_key(|k| k[3..].parse::<u32>().unwrap_or(u32::MAX));
                m.cores =
                    names.iter().map(|k| self.cpu.get(*k).and_then(|p| busy_pct(*p, cur[*k])).unwrap_or(0.0)).collect();
            }
            self.cpu = cur;
        }

        let mem = meminfo(&read(&self.proc_root.join("meminfo")).unwrap_or_default());
        if let Some(&total) = mem.get("MemTotal") {
            let avail = mem
                .get("MemAvailable")
                .copied()
                .unwrap_or_else(|| mem.get("MemFree").copied().unwrap_or(0) + mem.get("Cached").copied().unwrap_or(0));
            m.mem_total_mib = total / 1024;
            m.mem_used_mib = total.saturating_sub(avail) / 1024;
        }
        if let Some(&swap) = mem.get("SwapTotal").filter(|&&s| s > 0) {
            let free = mem.get("SwapFree").copied().unwrap_or(0);
            m.swap_pct = Some(100.0 * swap.saturating_sub(free) as f64 / swap as f64);
        }

        m.disk_pct = disk_pct(&self.disk_path);
        if let Some(l) = read(&self.proc_root.join("loadavg")) {
            m.load1 = l.split_whitespace().next().and_then(|v| v.parse().ok()).unwrap_or(0.0);
        }
        if let Some(u) = read(&self.proc_root.join("uptime")) {
            m.uptime_s = u.split_whitespace().next().and_then(|v| v.parse().ok()).unwrap_or(0.0);
        }

        if let Some(net) = read(&self.proc_root.join("net/dev")) {
            let (rx, tx) = net_bytes(&net, &self.net_exclude);
            if let Some((t0, rx0, tx0)) = self.net {
                if now > t0 {
                    let dt = now - t0;
                    m.net_rx_bps = rx.saturating_sub(rx0) as f64 / dt;
                    m.net_tx_bps = tx.saturating_sub(tx0) as f64 / dt;
                }
            }
            self.net = Some((now, rx, tx));
        }

        m.cpu_temp_c = self.cpu_temp_file.as_deref().and_then(milli_degrees);
        self.gpu(&mut m);
        m.clock = (self.localtime)();
        m
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cpu_rates() {
        let a = cpu_times("cpu  100 0 100 800 0 0 0 0 0 0\ncpu0 50 0 50 400 0 0 0 0\n");
        let b = cpu_times("cpu  200 0 100 900 0 0 0 0 0 0\ncpu0 150 0 50 400 0 0 0 0\n");
        assert_eq!(busy_pct(a["cpu"], b["cpu"]), Some(50.0));
        assert_eq!(busy_pct(a["cpu0"], b["cpu0"]), Some(100.0));
        assert_eq!(busy_pct(a["cpu"], a["cpu"]), None);
    }
}
