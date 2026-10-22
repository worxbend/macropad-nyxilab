//! The bridge: finds the macropad, feeds it metrics and text, runs commands for its HOST keys.
//!
//! One thread, one `poll(2)`: the device's hidraw node, the control socket that
//! nyxctl talks to, and two timers (metrics, device discovery).  Unplugging and
//! replugging the macropad, or restarting it, needs no action: the daemon reconnects.

use std::io::{self, BufRead, BufReader, Write};
use std::os::fd::{AsRawFd, RawFd};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use serde_json::{json, Value};

use crate::config::Config;
use crate::device::Device;
use crate::metrics::Source;
use crate::protocol::{self as proto, Event, Packet, TEXT_SLOTS};
use crate::{debug, info, warn};

/// Seconds between looks for a (re)connected macropad.
pub const SCAN_EVERY: f64 = 2.0;
/// Warn if HELLO is not answered by then (firmware without the host link).
pub const ACK_WAIT: f64 = 3.0;

pub type Finder = Box<dyn FnMut() -> Vec<String>>;
pub type Opener = Box<dyn FnMut(&str) -> io::Result<Box<dyn Device>>>;
pub type Clock = Box<dyn Fn() -> f64>;
pub type Spawner = Box<dyn FnMut(&str)>;

/// The control socket nyxctl uses: $XDG_RUNTIME_DIR/nyxd.sock, else /tmp/nyxd-<uid>.sock.
pub fn control_path() -> PathBuf {
    match std::env::var_os("XDG_RUNTIME_DIR") {
        Some(run) => PathBuf::from(run).join("nyxd.sock"),
        None => PathBuf::from(format!("/tmp/nyxd-{}.sock", unsafe { libc::getuid() })),
    }
}

/// Run a HOST key's command through /bin/sh, detached, without waiting for it.
pub fn spawn_shell(cmd: &str) {
    use std::os::unix::process::CommandExt;
    let child = Command::new("/bin/sh")
        .args(["-c", cmd])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .process_group(0)
        .spawn();
    match child {
        // reap it in the background so it does not linger as a zombie
        Ok(mut c) => drop(std::thread::spawn(move || c.wait())),
        Err(e) => warn!("cannot run {cmd:?}: {e}"),
    }
}

pub fn hostname() -> String {
    std::fs::read_to_string("/proc/sys/kernel/hostname").map(|s| s.trim().to_string()).unwrap_or_default()
}

pub struct Daemon {
    cfg: Config,
    source: Box<dyn Source>,
    find: Finder,
    open: Opener,
    clock: Clock,
    spawn: Spawner,
    hostname: String,
    dev: Option<Box<dyn Device>>,
    ack: Option<(u8, u8, u8, String)>, // edition, layers, protocol, firmware
    state: Option<(bool, u8, u8)>,     // dashboard shown, base, top
    texts: Vec<String>,
    next_scan: f64,
    next_metrics: f64,
    hello_at: Option<f64>,
    sent: u64,
    waiting_logged: bool,
    server: Option<(UnixListener, PathBuf)>,
}

impl Daemon {
    pub fn new(cfg: Config, source: Box<dyn Source>, find: Finder, open: Opener, clock: Clock, spawn: Spawner) -> Self {
        let mut texts = vec![String::new(); TEXT_SLOTS];
        for (&slot, value) in &cfg.text {
            texts[slot] = value.clone();
        }
        Daemon {
            cfg,
            source,
            find,
            open,
            clock,
            spawn,
            hostname: hostname(),
            dev: None,
            ack: None,
            state: None,
            texts,
            next_scan: 0.0,
            next_metrics: 0.0,
            hello_at: None,
            sent: 0,
            waiting_logged: false,
            server: None,
        }
    }

    pub fn set_hostname(&mut self, name: &str) {
        self.hostname = name.to_string();
    }

    pub fn connected(&self) -> bool {
        self.dev.is_some()
    }

    // ---------------------------------------------------------------- device
    fn connect(&mut self, now: f64) {
        self.next_scan = now + SCAN_EVERY;
        let paths = match &self.cfg.device {
            Some(d) => vec![d.clone()],
            None => (self.find)(),
        };
        if paths.is_empty() && !self.waiting_logged {
            info!("no macropad yet; waiting for one with the host-link firmware to be plugged in");
            self.waiting_logged = true;
        }
        for path in paths {
            match (self.open)(&path) {
                Ok(dev) => {
                    info!("macropad found at {path}");
                    self.dev = Some(dev);
                    self.ack = None;
                    self.state = None;
                    self.hello(now);
                    return;
                }
                Err(e) => warn!("cannot open {path}: {e} (is the udev rule installed?)"),
            }
        }
    }

    /// Start a session: HELLO, then everything the device should show besides metrics.
    fn hello(&mut self, now: f64) {
        self.hello_at = Some(now);
        if self.send(&proto::hello(&self.hostname)) {
            for slot in 0..TEXT_SLOTS {
                if !self.texts[slot].is_empty() {
                    let pkt = proto::text(slot, &self.texts[slot]).expect("valid slot");
                    self.send(&pkt);
                }
            }
        }
        self.next_metrics = now; // the first update right away
    }

    fn disconnect(&mut self, why: &str) {
        if self.dev.take().is_some() {
            info!("macropad disconnected ({why})");
        }
        self.ack = None;
        self.state = None;
        self.hello_at = None;
        self.next_scan = (self.clock)() + SCAN_EVERY;
    }

    fn send(&mut self, pkt: &Packet) -> bool {
        let Some(dev) = self.dev.as_mut() else { return false };
        match dev.write_packet(pkt) {
            Ok(ok) => {
                self.sent += u64::from(ok);
                ok
            }
            Err(e) => {
                self.disconnect(&e.to_string());
                false
            }
        }
    }

    fn on_device(&mut self) {
        while let Some(dev) = self.dev.as_mut() {
            match dev.read_packet() {
                Ok(None) => return,
                Ok(Some(p)) if p.is_empty() => return self.disconnect("end of file"),
                Ok(Some(p)) => self.on_event(proto::decode(&p)),
                Err(e) => return self.disconnect(&e.to_string()),
            }
        }
    }

    fn on_event(&mut self, ev: Option<Event>) {
        match ev {
            Some(Event::HelloAck { edition, n_layers, proto, firmware }) => {
                if self.hello_at.is_none() {
                    // unrequested: the device lost our session (link timeout)
                    info!("the macropad restarted its session; sending everything again");
                    let now = (self.clock)();
                    return self.hello(now);
                }
                info!(
                    "connected: {firmware}, {} edition, {n_layers} layers, protocol {proto}",
                    proto::edition_name(edition)
                );
                self.ack = Some((edition, n_layers, proto, firmware));
                self.hello_at = None;
            }
            Some(Event::State { host_view, base, top }) => {
                if self.state != Some((host_view, base, top)) {
                    debug!("dashboard {} (base layer {base}, top {top})", if host_view { "shown" } else { "hidden" });
                }
                self.state = Some((host_view, base, top));
            }
            Some(Event::Key { id, pressed: true }) => match self.cfg.keys.get(&id).cloned() {
                Some(cmd) => {
                    info!("HOST({id}): {cmd}");
                    (self.spawn)(&cmd);
                }
                None => info!("HOST({id}) pressed; map it to a command in [keys] of the config"),
            },
            Some(Event::Key { pressed: false, .. }) | None => {}
        }
    }

    // --------------------------------------------------------------- control
    /// Listen for nyxctl on `path` (owner-only permissions).
    pub fn listen(&mut self, path: &Path) -> io::Result<()> {
        if path.exists() {
            if UnixStream::connect(path).is_ok() {
                return Err(io::Error::new(io::ErrorKind::AddrInUse, "nyxd is already running"));
            }
            std::fs::remove_file(path)?; // left over from a crash
        }
        let old = unsafe { libc::umask(0o177) }; // anyone who can connect can change the display
        let listener = UnixListener::bind(path);
        unsafe { libc::umask(old) };
        let listener = listener?;
        listener.set_nonblocking(true)?;
        self.server = Some((listener, path.to_path_buf()));
        Ok(())
    }

    fn on_client(&mut self) {
        let Some((listener, _)) = &self.server else { return };
        let Ok((conn, _)) = listener.accept() else { return };
        let _ = conn.set_nonblocking(false);
        let _ = conn.set_read_timeout(Some(Duration::from_secs(1)));
        let mut line = String::new();
        let reply = match BufReader::new(&conn).take_line(&mut line) {
            Ok(()) => match serde_json::from_str::<Value>(line.trim()) {
                Ok(req) => self.handle(&req),
                Err(e) => json!({"ok": false, "error": e.to_string()}),
            },
            Err(e) => json!({"ok": false, "error": e.to_string()}),
        };
        let _ = (&conn).write_all(format!("{reply}\n").as_bytes());
    }

    /// One nyxctl request: {"cmd": "status" | "text" | "clear", ...}.
    pub fn handle(&mut self, req: &Value) -> Value {
        match req.get("cmd").and_then(Value::as_str) {
            Some("status") => json!({
                "ok": true,
                "connected": self.dev.is_some(),
                "device": self.dev.as_ref().map(|d| d.path().to_string()),
                "firmware": self.ack.as_ref().map(|a| a.3.clone()),
                "edition": self.ack.as_ref().map(|a| proto::edition_name(a.0)),
                "dashboard_visible": self.state.map(|s| s.0),
                "packets_sent": self.sent,
                "text": self.texts,
            }),
            Some("text") => {
                let slot = req.get("slot").and_then(Value::as_u64).map(|s| s as usize).filter(|s| *s < TEXT_SLOTS);
                let Some(slot) = slot else {
                    return json!({"ok": false, "error": format!("slot must be 0..{}", TEXT_SLOTS - 1)});
                };
                self.texts[slot] = req.get("text").and_then(Value::as_str).unwrap_or_default().to_string();
                let pkt = proto::text(slot, &self.texts[slot]).expect("valid slot");
                self.send(&pkt);
                json!({"ok": true, "delivered": self.dev.is_some()})
            }
            Some("clear") => {
                let slots: Vec<usize> = match req.get("slot").and_then(Value::as_u64) {
                    Some(s) => vec![s as usize],
                    None => (0..TEXT_SLOTS).collect(),
                };
                for slot in slots.into_iter().filter(|s| *s < TEXT_SLOTS) {
                    self.texts[slot].clear();
                    self.send(&proto::text(slot, "").expect("valid slot"));
                }
                json!({"ok": true})
            }
            other => json!({"ok": false, "error": format!("unknown command {other:?}")}),
        }
    }

    // ------------------------------------------------------------------ loop
    pub fn step(&mut self, max_wait: f64) {
        let now = (self.clock)();
        if self.dev.is_none() && now >= self.next_scan {
            self.connect(now);
        }
        if self.dev.is_some() && now >= self.next_metrics {
            self.next_metrics = now + self.cfg.interval;
            let m = self.source.sample();
            self.send(&proto::metrics(&m));
        }
        if self.dev.is_some() && self.hello_at.is_some_and(|t| now - t > ACK_WAIT) {
            warn!("the macropad does not answer; its firmware may predate the host link");
            self.hello_at = None;
        }

        let deadline = if self.dev.is_some() { self.next_metrics } else { self.next_scan };
        let wait = (deadline - now).clamp(0.0, max_wait.max(0.0));
        let mut fds: Vec<libc::pollfd> = Vec::with_capacity(2);
        let dev_fd: Option<RawFd> = self.dev.as_ref().map(|d| d.fd());
        if let Some(fd) = dev_fd {
            fds.push(libc::pollfd { fd, events: libc::POLLIN, revents: 0 });
        }
        if let Some((l, _)) = &self.server {
            fds.push(libc::pollfd { fd: l.as_raw_fd(), events: libc::POLLIN, revents: 0 });
        }
        let n = unsafe { libc::poll(fds.as_mut_ptr(), fds.len() as libc::nfds_t, (wait * 1000.0).ceil() as i32) };
        if n <= 0 {
            return; // timeout, or a signal (EINTR): the caller checks for stop
        }
        let ready = |fd: RawFd| fds.iter().any(|p| p.fd == fd && p.revents != 0);
        if dev_fd.is_some_and(ready) {
            self.on_device();
        }
        if self.server.as_ref().is_some_and(|(l, _)| ready(l.as_raw_fd())) {
            self.on_client();
        }
    }

    pub fn run(&mut self, stop: Arc<AtomicBool>) {
        info!(
            "nyxd {} started; control socket {}",
            env!("CARGO_PKG_VERSION"),
            self.server.as_ref().map_or("off".into(), |(_, p)| p.display().to_string())
        );
        while !stop.load(Ordering::Relaxed) {
            self.step(0.5);
        }
        self.close();
    }

    pub fn close(&mut self) {
        if self.dev.is_some() {
            self.send(&proto::bye()); // the dashboard switches to "waiting for host" at once
            self.disconnect("stopping");
        }
        if let Some((_, path)) = self.server.take() {
            let _ = std::fs::remove_file(path);
        }
    }
}

impl Drop for Daemon {
    fn drop(&mut self) {
        self.close();
    }
}

trait TakeLine {
    fn take_line(self, line: &mut String) -> io::Result<()>;
}

impl<R: BufRead> TakeLine for R {
    /// One request line, at most 4 KiB.
    fn take_line(self, line: &mut String) -> io::Result<()> {
        let mut limited = self.take(4096);
        limited.read_line(line).map(|_| ())
    }
}

/// Send one request to a running daemon (what nyxctl does).
pub fn request(req: &Value, path: &Path) -> io::Result<Value> {
    let conn = UnixStream::connect(path)?;
    conn.set_read_timeout(Some(Duration::from_secs(2)))?;
    (&conn).write_all(format!("{req}\n").as_bytes())?;
    let mut line = String::new();
    BufReader::new(&conn).read_line(&mut line)?;
    serde_json::from_str(line.trim()).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))
}
