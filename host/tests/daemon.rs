//! The daemon loop against a fake macropad, with a fake clock.

mod common;

use std::cell::{Cell, RefCell};
use std::rc::Rc;

use common::{FakeMacropad, TempDir};
use nyxd::config::Config;
use nyxd::daemon::{request, Daemon, SCAN_EVERY};
use nyxd::device::Device;
use nyxd::metrics::Source;
use nyxd::protocol::{self as proto, Metrics};
use serde_json::json;

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

fn rig() -> Rig {
    let now = Rc::new(Cell::new(1000.0));
    let pad = Rc::new(RefCell::new(FakeMacropad::new()));
    let spawned = Rc::new(RefCell::new(Vec::new()));
    let found = Rc::new(RefCell::new(vec!["/dev/hidraw7".to_string()]));
    let mut cfg = Config::default();
    cfg.keys.insert(3, "echo hi".into());
    cfg.text.insert(0, "boot text".into());
    let (n, p, s, f) = (now.clone(), pad.clone(), spawned.clone(), found.clone());
    let mut d = Daemon::new(
        cfg,
        Box::new(FakeSource),
        Box::new(move || f.borrow().clone()),
        Box::new(move |path| Ok(Box::new(p.borrow().device(path)) as Box<dyn Device>)),
        Box::new(move || n.get()),
        Box::new(move |cmd| s.borrow_mut().push(cmd.to_string())),
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
}

#[test]
fn session() {
    let mut r = rig();
    r.step();
    let first = r.pad.borrow().received();
    assert_eq!(first.iter().map(|p| p[0]).collect::<Vec<_>>(), [proto::HELLO, proto::TEXT, proto::METRICS]);
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
    assert_eq!(*r.spawned.borrow(), ["echo hi"]);
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
    assert_eq!(r.kinds(), [proto::HELLO, proto::TEXT, proto::TEXT, proto::METRICS]);

    r.d.close();
    assert_eq!(r.kinds(), [proto::BYE]);
    assert!(!r.d.connected());
}

#[test]
fn reconnects_after_unplug() {
    let mut r = rig();
    r.step();
    r.pad.borrow_mut().unplug();
    r.step();
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
