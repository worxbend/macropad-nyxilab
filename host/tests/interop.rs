//! The daemon's encoder against the firmware's real parser
//! (tests/peer.cpp + common/firmware/lib/nyx_core/src/core/host_proto.cpp).

mod common;

use std::io::Write;
use std::path::Path;
use std::process::{Command, Stdio};

use common::TempDir;
use nyxd::protocol::{self as proto, Event, Metrics};

#[test]
fn firmware_parser_understands_the_daemon() {
    if Command::new("g++").arg("--version").output().is_err() {
        eprintln!("skipped: needs g++");
        return;
    }
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let core = root.join("../common/firmware/lib/nyx_core/src");
    let tmp = TempDir::new("interop");
    let exe = tmp.path().join("peer");
    let built = Command::new("g++")
        .args(["-std=c++17", "-Wall", "-Wextra", "-Werror"])
        .arg(format!("-I{}", core.display()))
        .arg(root.join("tests/peer.cpp"))
        .arg(core.join("core/host_proto.cpp"))
        .arg("-o")
        .arg(&exe)
        .status()
        .unwrap();
    assert!(built.success(), "peer build failed");

    let mut cores = vec![10.0, 20.6, 30.0, 100.0];
    cores.extend([50.0; 16]);
    let m = Metrics {
        cpu_pct: Some(37.4),
        cores,
        mem_used_mib: 11674,
        mem_total_mib: 31890,
        swap_pct: Some(3.0),
        disk_pct: Some(61.2),
        cpu_temp_c: Some(54.66),
        load1: 2.14,
        net_rx_bps: 1_840_000.7,
        net_tx_bps: 96_300.0,
        uptime_s: 266_520.9,
        gpu_pct: None,
        gpu_temp_c: Some(46.1),
        clock: Some((14, 7)),
    };
    let mut input = Vec::new();
    input.extend(proto::hello("désk-01"));
    input.extend(proto::metrics(&m));
    input.extend(proto::text(2, "build ✓ ok").unwrap());

    let mut child =
        Command::new(&exe).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().unwrap();
    child.stdin.take().unwrap().write_all(&input).unwrap();
    let out = child.wait_with_output().unwrap();
    assert!(out.status.success());

    let replies: Vec<Option<Event>> = out.stdout.chunks(proto::PACKET).map(proto::decode).collect();
    assert_eq!(
        replies,
        [
            Some(Event::HelloAck {
                edition: 2,
                n_layers: 5,
                proto: proto::PROTO_VERSION,
                firmware: "peer firmware".into()
            }),
            Some(Event::State { host_view: true, base: 3, top: 3 }),
            Some(Event::Key { id: 5, pressed: true }),
        ]
    );

    let seen: serde_json::Value = serde_json::from_slice(&out.stderr).unwrap();
    assert_eq!(seen["linked"], 1);
    assert_eq!(seen["hostname"], "desk-01"); // accent stripped by the daemon
    assert_eq!(seen["cpu"], 37);
    assert_eq!(seen["cores"].as_array().unwrap().len(), 16); // 20 cores averaged into 16 bars
    assert_eq!(seen["cores"][0], 10);
    assert_eq!(seen["cores"][1], 21);
    assert_eq!((seen["mem_used"].clone(), seen["mem_total"].clone()), (11674.into(), 31890.into()));
    assert_eq!((seen["swap"].clone(), seen["disk"].clone()), (3.into(), 61.into()));
    assert_eq!((seen["temp10"].clone(), seen["load100"].clone()), (547.into(), 214.into()));
    assert_eq!(seen["rx"], 1_840_000);
    assert_eq!(seen["tx"], 96_300);
    assert_eq!(seen["up"], 266_520);
    assert_eq!((seen["gpu"].clone(), seen["gpu_temp10"].clone()), (255.into(), 461.into()));
    assert_eq!((seen["h"].clone(), seen["m"].clone()), (14.into(), 7.into()));
    assert_eq!(seen["text"], serde_json::json!(["", "", "build ? ok", ""]));
}
