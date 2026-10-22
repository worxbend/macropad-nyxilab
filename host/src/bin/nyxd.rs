//! nyxd: shows this computer's CPU, memory, ... on the Nyxilab macropad's HOST layer.

use std::path::PathBuf;
use std::process::ExitCode;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::Instant;

use nyxd::daemon::{self, Daemon};
use nyxd::device::{find_system_devices, Device, HidrawDevice};
use nyxd::metrics::{Collector, Options, Source};
use nyxd::{config, log};

const USAGE: &str = "usage: nyxd [--config PATH] [--device /dev/hidrawN] [--interval S] [--socket PATH] [--probe] [-v]

  --config PATH    configuration file (default: ~/.config/nyxd/config.toml)
  --device PATH    use this hidraw node instead of searching
  --interval S     seconds between updates, 0.2 .. 4 (default 1)
  --socket PATH    control socket for nyxctl (default: $XDG_RUNTIME_DIR/nyxd.sock)
  --probe          list macropads and print one metrics sample, then exit
  -v, --verbose    also log dashboard visibility changes
  --version";

struct Args {
    config: PathBuf,
    device: Option<String>,
    interval: Option<f64>,
    socket: PathBuf,
    probe: bool,
    verbose: bool,
}

fn parse_args() -> Result<Args, String> {
    let mut a = Args {
        config: config::default_path(),
        device: None,
        interval: None,
        socket: daemon::control_path(),
        probe: false,
        verbose: false,
    };
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        let mut value = |name: &str| it.next().ok_or(format!("{name} needs a value"));
        match arg.as_str() {
            "--config" => a.config = value("--config")?.into(),
            "--device" => a.device = Some(value("--device")?),
            "--interval" => {
                let v = value("--interval")?;
                let i: f64 = v.parse().map_err(|_| format!("--interval: not a number: {v}"))?;
                if !(0.2..=4.0).contains(&i) {
                    return Err("--interval must be 0.2 .. 4".into());
                }
                a.interval = Some(i);
            }
            "--socket" => a.socket = value("--socket")?.into(),
            "--probe" => a.probe = true,
            "-v" | "--verbose" => a.verbose = true,
            "--version" => {
                println!("nyxd {}", env!("CARGO_PKG_VERSION"));
                std::process::exit(0);
            }
            "-h" | "--help" => {
                println!("{USAGE}");
                std::process::exit(0);
            }
            other => return Err(format!("unknown argument {other:?}\n\n{USAGE}")),
        }
    }
    Ok(a)
}

fn main() -> ExitCode {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("nyxd: {e}");
            return ExitCode::from(2);
        }
    };
    log::set_verbose(args.verbose);
    let mut cfg = match config::load(&args.config) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("nyxd: {e}");
            return ExitCode::from(2);
        }
    };
    if args.device.is_some() {
        cfg.device = args.device;
    }
    if let Some(i) = args.interval {
        cfg.interval = i;
    }
    let mut collector = Collector::new(Options {
        disk_path: cfg.disk_path.clone(),
        gpu: cfg.gpu,
        net_exclude: cfg.net_exclude.clone(),
        ..Options::default()
    });

    if args.probe {
        std::thread::sleep(std::time::Duration::from_secs(1)); // a rate needs two samples
        let found = find_system_devices();
        println!("macropads: {}", if found.is_empty() { "none found".into() } else { found.join(", ") });
        println!("{}", serde_json::to_string_pretty(&collector.sample()).expect("metrics serialize"));
        return ExitCode::SUCCESS;
    }

    let start = Instant::now();
    let mut d = Daemon::new(
        cfg,
        Box::new(collector),
        Box::new(find_system_devices),
        Box::new(|p| HidrawDevice::open(p).map(|d| Box::new(d) as Box<dyn Device>)),
        Box::new(move || start.elapsed().as_secs_f64()),
        Box::new(daemon::spawn_shell),
    );
    if let Err(e) = d.listen(&args.socket) {
        eprintln!("nyxd: {}: {e}", args.socket.display());
        return ExitCode::from(1);
    }
    let stop = Arc::new(AtomicBool::new(false));
    for sig in [signal_hook::consts::SIGTERM, signal_hook::consts::SIGINT] {
        signal_hook::flag::register(sig, Arc::clone(&stop)).expect("signal handler");
    }
    d.run(stop);
    ExitCode::SUCCESS
}
