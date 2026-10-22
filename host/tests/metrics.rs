//! The collector against a fake /proc and /sys.

mod common;

use std::cell::Cell;
use std::rc::Rc;

use common::TempDir;
use nyxd::metrics::{Collector, Gpu, Options, Source};

const STAT_1: &str =
    "cpu  100 0 100 800 0 0 0 0 0 0\ncpu0 50 0 50 400 0 0 0 0 0 0\ncpu1 50 0 50 400 0 0 0 0 0 0\nintr 1 2 3\n";
// +100 busy / +200 total overall: cpu0 fully busy, cpu1 idle
const STAT_2: &str = "cpu  200 0 100 900 0 0 0 0 0 0\ncpu0 150 0 50 400 0 0 0 0 0 0\ncpu1 50 0 50 500 0 0 0 0 0 0\n";
const MEMINFO: &str = "MemTotal:       16384000 kB\nMemFree:         1000000 kB\nMemAvailable:    8192000 kB\nSwapTotal:       2000000 kB\nSwapFree:        1500000 kB\n";
const NETDEV_1: &str = "Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
    lo: 999999 0 0 0 0 0 0 0 999999 0 0 0 0 0 0 0
  eth0: 1000 0 0 0 0 0 0 0 500 0 0 0 0 0 0 0
docker0: 777777 0 0 0 0 0 0 0 777777 0 0 0 0 0 0 0
";

fn fixture() -> TempDir {
    let t = TempDir::new("metrics");
    t.write("proc/stat", STAT_1);
    t.write("proc/meminfo", MEMINFO);
    t.write("proc/loadavg", "1.23 0.50 0.25 1/100 999\n");
    t.write("proc/uptime", "93784.12 1000.00\n");
    t.write("proc/net/dev", NETDEV_1);
    t.write("sys/class/hwmon/hwmon0/name", "nvme\n");
    t.write("sys/class/hwmon/hwmon0/temp1_input", "38000\n");
    t.write("sys/class/hwmon/hwmon3/name", "k10temp\n");
    t.write("sys/class/hwmon/hwmon3/temp1_input", "54660\n");
    t.write("sys/class/drm/card1/device/gpu_busy_percent", "12\n");
    t.write("sys/class/drm/card1/device/hwmon/hwmon5/temp1_input", "46000\n");
    t.write("sys/class/drm/card1-DP-1/status", "connected\n"); // a connector, not a GPU
    t
}

fn collector(t: &TempDir, now: Rc<Cell<f64>>, gpu: Gpu, sys: &str) -> Collector {
    Collector::with_clock(
        Options {
            proc_root: t.path().join("proc"),
            sys_root: t.path().join(sys),
            disk_path: t.path().to_path_buf(),
            gpu,
            ..Options::default()
        },
        Box::new(move || now.get()),
        Box::new(|| Some((14, 7))),
    )
}

#[test]
fn rates_and_values() {
    let t = fixture();
    let now = Rc::new(Cell::new(100.0));
    let mut c = collector(&t, now.clone(), Gpu::Amd, "sys");
    t.write("proc/stat", STAT_2);
    t.write("proc/net/dev", NETDEV_1.replace("eth0: 1000", "eth0: 5000").replace("0 500 0", "0 2500 0"));
    now.set(102.0);
    let m = c.sample();
    assert_eq!(m.cpu_pct, Some(50.0));
    assert_eq!(m.cores, vec![100.0, 0.0]);
    assert_eq!((m.mem_used_mib, m.mem_total_mib), (8000, 16000));
    assert_eq!(m.swap_pct, Some(25.0));
    assert_eq!(m.load1, 1.23);
    assert_eq!(m.uptime_s as u64, 93784);
    assert_eq!((m.net_rx_bps, m.net_tx_bps), (2000.0, 1000.0)); // lo and docker0 left out
    assert_eq!(m.cpu_temp_c, Some(54.66)); // k10temp preferred over the NVMe sensor
    assert_eq!((m.gpu_pct, m.gpu_temp_c), (Some(12.0), Some(46.0)));
    assert_eq!(m.clock, Some((14, 7)));
    assert!(m.disk_pct.is_some_and(|d| (0.0..=100.0).contains(&d)));
}

#[test]
fn first_sample_has_no_cpu_rate_and_missing_files_are_unknown() {
    let t = fixture();
    std::fs::remove_file(t.path().join("proc/meminfo")).unwrap();
    std::fs::remove_file(t.path().join("proc/loadavg")).unwrap();
    let mut c = collector(&t, Rc::new(Cell::new(100.0)), Gpu::Off, "nosys");
    let m = c.sample(); // same counters as the priming sample
    assert_eq!(m.cpu_pct, None);
    assert_eq!(m.mem_total_mib, 0);
    assert_eq!(m.swap_pct, None);
    assert_eq!(m.cpu_temp_c, None);
    assert_eq!(m.gpu_pct, None);
}

#[test]
fn this_machine() {
    let m = Collector::new(Options { gpu: Gpu::Off, ..Options::default() }).sample(); // must not panic
    assert!(m.mem_total_mib > 0);
}
