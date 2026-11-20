//! The daemon loop against a fake macropad, with a fake clock.

mod common;

use std::cell::{Cell, RefCell};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::rc::Rc;
use std::sync::{Mutex, MutexGuard};
use std::time::{Duration, Instant};

use common::{FakeMacropad, TempDir};
use nyxd::config::Config;
use nyxd::daemon::{request, Daemon, SCAN_EVERY};
use nyxd::device::Device;
use nyxd::metrics::Source;
use nyxd::protocol::{self as proto, Metrics};
use serde_json::json;

/// Held by the tests that create files: `Daemon::listen` sets the process-wide umask to
/// 0177 around bind, so a directory another test thread creates meanwhile gets no x bit.
static FILES: Mutex<()> = Mutex::new(());

fn files() -> MutexGuard<'static, ()> {
    FILES.lock().unwrap_or_else(|e| e.into_inner())
}

struct FakeSource;

impl Source for FakeSource {
    fn sample(&mut self) -> Metrics {
        Metrics {
            cpu_pct: Some(42.0),
            cores: vec![1.0, 2.0],
            mem_used_mib: 1,
            mem_total_mib: 2,
            clock: Some((9, 30)),
            ..Metrics::default()
        }
    }
}

struct Rig {
    d: Daemon,
    now: Rc<Cell<f64>>,
    pad: Rc<RefCell<FakeMacropad>>,
    spawned: Rc<RefCell<Vec<String>>>,
    found: Rc<RefCell<Vec<String>>>,
}

fn ack() -> Vec<u8> {
    let mut p = vec![proto::HELLO_ACK, proto::PROTO_VERSION, 2, 5, 1];
    p.extend_from_slice(b"fake fw");
    p
}

/// A command as one string: program and arguments, space-separated.
fn argv(cmd: &Command) -> String {
    let mut parts = vec![cmd.get_program().to_string_lossy().into_owned()];
    parts.extend(cmd.get_args().map(|a| a.to_string_lossy().into_owned()));
    parts.join(" ")
}

fn rig() -> Rig {
    let now = Rc::new(Cell::new(1000.0));
    let pad = Rc::new(RefCell::new(FakeMacropad::new()));
    let spawned = Rc::new(RefCell::new(Vec::new()));
    let found = Rc::new(RefCell::new(vec!["/dev/hidraw7".to_string()]));
    let mut cfg = Config::default();
    cfg.keys.insert(3, "echo hi".into());
    cfg.keys.insert(212, "my next".into()); // [keys] wins over the player's own ids
    cfg.text.insert(0, "boot text".into());
    let (n, p, s, f) = (now.clone(), pad.clone(), spawned.clone(), found.clone());
    let mut d = Daemon::new(
        cfg,
        Box::new(FakeSource),
        Box::new(move || f.borrow().clone()),
        Box::new(move |path| Ok(Box::new(p.borrow().device(path)) as Box<dyn Device>)),
        Box::new(move || n.get()),
        Box::new(move |cmd: Command| s.borrow_mut().push(argv(&cmd))),
    );
    d.set_hostname("box");
    Rig { d, now, pad, spawned, found }
}

impl Rig {
    fn step(&mut self) {
        for _ in 0..3 {
            self.d.step(0.0);
        }
    }
    fn kinds(&self) -> Vec<u8> {
        self.pad.borrow().kinds()
    }
    /// Step on real time (a real playerctl child) until `done` holds for what the pad received.
    fn received_until(&mut self, done: impl Fn(&[Vec<u8>]) -> bool) -> Vec<Vec<u8>> {
        let mut got = Vec::new();
        let start = Instant::now();
        while !done(&got) {
            assert!(start.elapsed() < Duration::from_secs(5), "gave up waiting; received {got:?}");
            self.d.step(0.02);
            std::thread::sleep(Duration::from_millis(5));
            got.extend(self.pad.borrow().received());
        }
        got
    }
}

const SESSION: [u8; 6] =
    [proto::WORKSPACES, proto::PLAYER, proto::PLAYER_TEXT, proto::PLAYER_TEXT, proto::PLAYER_TEXT, proto::METRICS];

fn player_line(fields: [&str; 9]) -> String {
    fields.join("\x1f")
}

/// A fake playerctl: prints `lines` when followed, then waits `linger` seconds and exits;
/// logs every other call's arguments to `calls`.
fn fake_playerctl(tmp: &TempDir, lines: &[String], linger: f64) -> PathBuf {
    let script = tmp.path().join("playerctl");
    let body: String = lines.iter().map(|l| format!("printf '%s\\n' '{l}'\n")).collect();
    let calls = tmp.path().join("calls");
    tmp.write(
        "playerctl",
        format!(
            "#!/bin/sh\nif [ \"$1\" = --follow ]; then\n{body}sleep {linger}\nexit 0\nfi\necho \"$@\" >> {}\n",
            calls.display()
        ),
    );
    use std::os::unix::fs::PermissionsExt;
    std::fs::set_permissions(&script, std::fs::Permissions::from_mode(0o755)).unwrap();
    script
}

#[test]
fn session() {
    let mut r = rig();
    r.step();
    let first = r.pad.borrow().received();
    let kinds: Vec<u8> = first.iter().map(|p| p[0]).collect();
    assert_eq!(kinds[..2], [proto::HELLO, proto::TEXT]);
    assert_eq!(kinds[2..], SESSION); // a new session: sway and player state too, even when empty
    assert_eq!(first[3][2], 0); // no player
    assert_eq!(&first[0][2..5], b"box");
    assert_eq!(&first[1][3..12], b"boot text");

    r.pad.borrow().send(&ack());
    r.step();
    assert_eq!(r.d.handle(&json!({"cmd": "status"}))["edition"], "rotary");
    assert_eq!(r.kinds(), Vec::<u8>::new()); // nothing more until the next interval

    r.now.set(r.now.get() + 1.0);
    r.step();
    assert_eq!(r.kinds(), [proto::METRICS]);

    r.pad.borrow().send(&[proto::STATE, proto::PROTO_VERSION, 1, 3, 3]);
    r.pad.borrow().send(&[proto::KEY, proto::PROTO_VERSION, 3, 1]);
    r.pad.borrow().send(&[proto::KEY, proto::PROTO_VERSION, 3, 0]); // release: nothing
    r.pad.borrow().send(&[proto::KEY, proto::PROTO_VERSION, 9, 1]); // not mapped
    r.step();
    assert_eq!(*r.spawned.borrow(), ["/bin/sh -c echo hi"]);
    assert_eq!(r.d.handle(&json!({"cmd": "status"}))["dashboard_visible"], true);

    let reply = r.d.handle(&json!({"cmd": "text", "slot": 2, "text": "héllo"}));
    assert_eq!(reply, json!({"ok": true, "delivered": true}));
    let pkt = r.pad.borrow().received().remove(0);
    assert_eq!(&pkt[..3], &[proto::TEXT, proto::PROTO_VERSION, 2]);
    assert_eq!(&pkt[3..8], b"hello");
    assert_eq!(r.d.handle(&json!({"cmd": "text", "slot": 7, "text": "x"}))["ok"], false);
    assert_eq!(r.d.handle(&json!({"cmd": "bogus"}))["ok"], false);

    // the device timed out (e.g. the computer slept) and asks again: replay HELLO and text
    r.pad.borrow().send(&ack());
    r.step();
    let kinds = r.kinds();
    assert_eq!(kinds[..3], [proto::HELLO, proto::TEXT, proto::TEXT]);
    assert_eq!(kinds[3..], SESSION);

    r.d.close();
    assert_eq!(r.kinds(), [proto::BYE]);
    assert!(!r.d.connected());
}

#[test]
fn reconnects_after_unplug() {
    let mut r = rig();
    r.step();
    r.pad.borrow_mut().unplug();
    // a child that another test is spawning holds a copy of the socket until its exec
    let start = Instant::now();
    while r.d.connected() && start.elapsed() < Duration::from_secs(2) {
        r.step();
    }
    assert!(!r.d.connected());
    *r.pad.borrow_mut() = FakeMacropad::new();
    r.step();
    assert!(!r.d.connected()); // waits SCAN_EVERY between attempts
    r.now.set(r.now.get() + SCAN_EVERY);
    r.step();
    assert!(r.d.connected());
    assert_eq!(r.kinds()[0], proto::HELLO);
}

#[test]
fn no_device_found_yet() {
    let mut r = rig();
    r.found.borrow_mut().clear();
    r.step();
    assert!(!r.d.connected());
    assert_eq!(r.d.handle(&json!({"cmd": "status"}))["connected"], false);
    assert_eq!(r.d.handle(&json!({"cmd": "text", "slot": 1, "text": "later"}))["delivered"], false);
    r.found.borrow_mut().push("/dev/hidraw7".into());
    r.now.set(r.now.get() + SCAN_EVERY);
    r.step();
    assert_eq!(r.kinds().iter().filter(|&&k| k == proto::TEXT).count(), 2); // both slots set so far
}

#[test]
fn nyxctl_round_trip_over_the_socket() {
    let _files = files();
    let tmp = TempDir::new("ctl");
    let path = tmp.path().join("nyxd.sock");
    let mut r = rig();
    r.found.borrow_mut().clear();
    r.d.listen(&path).unwrap();
    use std::os::unix::fs::PermissionsExt;
    assert_eq!(std::fs::metadata(&path).unwrap().permissions().mode() & 0o777, 0o600);

    // a second daemon on the same socket is refused
    let mut other = rig();
    assert!(other.d.listen(&path).is_err());

    let p = path.clone();
    let client = std::thread::spawn(move || {
        [
            request(&json!({"cmd": "text", "slot": 1, "text": "from nyxctl"}), &p).unwrap(),
            request(&json!({"cmd": "status"}), &p).unwrap(),
            request(&json!({"cmd": "bogus"}), &p).unwrap(),
        ]
    });
    while !client.is_finished() {
        r.d.step(0.05);
    }
    let replies = client.join().unwrap();
    assert_eq!(replies[0]["ok"], true);
    assert_eq!(replies[1]["text"][1], "from nyxctl");
    assert_eq!(replies[2]["ok"], false);
    r.d.close();
    assert!(!path.exists());
}

#[test]
fn player_follows_playerctl_and_its_keys_control_the_player_shown() {
    let _files = files();
    let tmp = TempDir::new("player");
    let playing = player_line(["spotify", "spotify", "Playing", "61000000", "200000000", "0.5", "Song", "Band", "LP"]);
    let seeked = player_line(["spotify", "spotify", "Playing", "75000000", "200000000", "0.5", "Song", "Band", "LP"]);
    let mut r = rig();
    r.d.enable_player(fake_playerctl(&tmp, &[playing, seeked], 0.5));
    r.step();
    r.pad.borrow().send(&ack());

    // the two lines arrive together or apart; the newest wins, and the text goes once
    let got = r.received_until(|got| got.iter().any(|p| p[0] == proto::PLAYER && p[5] == 75));
    let player: Vec<&Vec<u8>> = got.iter().filter(|p| p[0] == proto::PLAYER && p[2] == 1).collect();
    let last = player.last().unwrap();
    assert_eq!(&last[2..9], &[1, 1, 50, 75, 0, 200, 0]);
    assert_eq!(&last[9..16], b"spotify");
    let texts: Vec<&Vec<u8>> = got.iter().filter(|p| p[0] == proto::PLAYER_TEXT && p[3] != 0).collect();
    assert_eq!(texts.len(), 3);
    assert_eq!(&texts[0][2..7], b"\x00Song");
    let status = r.d.handle(&json!({"cmd": "status"}));
    assert_eq!(status["player"]["status"], "playing");
    assert_eq!(status["player"]["title"], "Song");

    // while playing, every metrics tick sends PLAYER (the dashboard's position advances)
    r.now.set(r.now.get() + 1.0);
    r.d.step(0.0);
    assert!(r.kinds().contains(&proto::PLAYER));

    for id in [211, 212, 216, 218] {
        r.pad.borrow().send(&[proto::KEY, proto::PROTO_VERSION, id, 1]);
    }
    r.step();
    let program = tmp.path().join("playerctl").display().to_string();
    assert_eq!(
        *r.spawned.borrow(),
        [
            format!("{program} --player spotify play-pause"),
            "/bin/sh -c my next".to_string(),
            format!("{program} --player spotify position 10+"),
            format!("{program} --player spotify volume 0.05+"),
        ]
    );

    // playerctl exits: no player until it is back
    let got = r.received_until(|got| got.iter().any(|p| p[0] == proto::PLAYER && p[2] == 0));
    assert_eq!(got.iter().filter(|p| p[0] == proto::PLAYER_TEXT).count(), 3); // all three cleared
    assert_eq!(r.d.handle(&json!({"cmd": "status"}))["player"], json!(null));
    r.spawned.borrow_mut().clear();
    r.pad.borrow().send(&[proto::KEY, proto::PROTO_VERSION, 211, 1]);
    r.step();
    assert!(r.spawned.borrow().is_empty()); // nothing to control
    r.now.set(r.now.get() + SCAN_EVERY);
    r.received_until(|got| got.iter().any(|p| p[0] == proto::PLAYER && p[2] == 1));
    // started again
}

#[test]
fn missing_playerctl_means_no_player() {
    let mut r = rig();
    r.d.enable_player(Path::new("/nonexistent/playerctl").into());
    r.step();
    assert_eq!(r.d.handle(&json!({"cmd": "status"}))["player"], json!(null));
    assert!(r.d.connected());
}
