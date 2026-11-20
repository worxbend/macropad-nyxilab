//! The sway client against a fake sway on a Unix socket.

mod common;

use std::io::{Read, Write};
use std::os::unix::net::{UnixListener, UnixStream};
use std::thread;

use common::TempDir;
use nyxd::protocol::{Layout, Workspaces};
use nyxd::sway::{encode, Sway};
use serde_json::json;

/// A request as sway sees it: (type, payload bytes); commands are plain text, not JSON.
fn read_request(conn: &mut UnixStream) -> Option<(u32, String)> {
    let mut head = [0u8; 14];
    conn.read_exact(&mut head).ok()?;
    assert_eq!(&head[..6], b"i3-ipc");
    let len = u32::from_ne_bytes(head[6..10].try_into().unwrap()) as usize;
    let kind = u32::from_ne_bytes(head[10..14].try_into().unwrap());
    let mut payload = vec![0u8; len];
    conn.read_exact(&mut payload).ok()?;
    Some((kind, String::from_utf8(payload).unwrap()))
}

#[test]
fn client_subscribes_queries_and_switches() {
    let tmp = TempDir::new("sway");
    let path = tmp.path().join("sway-ipc.sock");
    let listener = UnixListener::bind(&path).unwrap();
    let server = thread::spawn(move || {
        let (mut events, _) = listener.accept().unwrap();
        let (mut requests, _) = listener.accept().unwrap();
        assert_eq!(read_request(&mut events), Some((2, r#"["workspace","window"]"#.to_string())));
        events.write_all(&encode(2, br#"{"success": true}"#)).unwrap();
        events.write_all(&encode(0x8000_0000, br#"{"change": "focus"}"#)).unwrap();
        let mut ran = Vec::new();
        while let Some((kind, payload)) = read_request(&mut requests) {
            let reply = match kind {
                1 => {
                    json!([{"num": 2, "name": "2", "focused": true, "visible": true, "urgent": false, "layout": "splitv"}])
                }
                4 => json!({"type": "root", "nodes": [{"type": "workspace", "name": "2", "nodes": [
                    {"type": "con", "name": "editor", "app_id": "code", "focused": true}
                ]}]}),
                0 => {
                    ran.push(payload);
                    json!([{"success": ran.len() == 1}])
                }
                _ => json!(null),
            };
            requests.write_all(&encode(kind, reply.to_string().as_bytes())).unwrap();
        }
        ran
    });

    let mut sway = Sway::connect(&path).unwrap();
    sway.take_event().unwrap();
    assert_eq!(
        sway.query().unwrap(),
        Workspaces {
            running: true,
            focused: 2,
            open: 0x02,
            visible: 0x02,
            windows: 1,
            layout: Layout::SplitV,
            app_id: "code".into(),
            title: "editor".into(),
            ..Workspaces::default()
        }
    );
    sway.switch_to(4).unwrap();
    assert!(sway.switch_to(5).is_err()); // sway said no
    drop(sway);
    assert_eq!(server.join().unwrap(), ["workspace number 4", "workspace number 5"]);
}

#[test]
fn refused_subscription_is_an_error() {
    let tmp = TempDir::new("sway-refused");
    let path = tmp.path().join("sway-ipc.sock");
    let listener = UnixListener::bind(&path).unwrap();
    let server = thread::spawn(move || {
        let (mut events, _) = listener.accept().unwrap();
        let _requests = listener.accept().unwrap();
        read_request(&mut events).unwrap();
        events.write_all(&encode(2, br#"{"success": false}"#)).unwrap();
    });
    assert!(Sway::connect(&path).is_err());
    server.join().unwrap();
}
