//! hidraw discovery against a fake sysfs, and the descriptor constants against the firmware.

mod common;

use common::{FakeMacropad, TempDir};
use nyxd::device::{find_devices, Device};
use nyxd::protocol as proto;

const KEYBOARD_DESC: &[u8] = &[0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01];
const HOST_LINK_DESC: &[u8] = &[0x06, 0x4E, 0xFF, 0x09, 0x58, 0xA1, 0x01];
const OTHER_VENDOR_DESC: &[u8] = &[0x06, 0x60, 0xFF, 0x09, 0x61, 0xA1, 0x01]; // e.g. a QMK raw-HID keyboard

#[test]
fn only_the_host_link_interface_matches() {
    let t = TempDir::new("sysfs");
    for (name, desc) in [
        ("hidraw0", KEYBOARD_DESC),
        ("hidraw11", HOST_LINK_DESC),
        ("hidraw2", OTHER_VENDOR_DESC),
        ("hidraw3", HOST_LINK_DESC),
    ] {
        t.write(&format!("{name}/device/report_descriptor"), desc);
    }
    std::fs::create_dir_all(t.path().join("hidraw9")).unwrap(); // no descriptor readable
    assert_eq!(find_devices(t.path(), "/dev".as_ref()), ["/dev/hidraw3", "/dev/hidraw11"]);
}

#[test]
fn constants_match_the_firmware() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("..");
    let hdr = std::fs::read_to_string(root.join("common/firmware/lib/nyx_core/src/core/host_proto.h")).unwrap();
    assert!(hdr.contains("HOST_USAGE_PAGE = 0xFF4E, HOST_USAGE = 0x58"));
    assert_eq!((proto::USAGE_PAGE, proto::USAGE), (0xFF4E, 0x58));
    assert!(hdr.contains(&format!("PROTO_VERSION = {};", proto::PROTO_VERSION)));
    let hid = std::fs::read_to_string(root.join("common/firmware/lib/nyx_hw/src/hw/host_hid.cpp")).unwrap();
    assert!(hid.contains(&format!("\"{}\"", proto::INTERFACE_NAME)));
    let rule = std::fs::read_to_string(root.join("host/udev/70-nyxilab-macropad.rules")).unwrap();
    assert!(rule.contains(&format!("ATTRS{{interface}}==\"{}\"", proto::INTERFACE_NAME)));
}

#[test]
fn hidraw_writes_carry_report_id_zero_and_reads_split_packets() {
    let pad = FakeMacropad::new();
    let mut dev = pad.device("/dev/fake");
    assert!(dev.write_packet(&proto::bye()).unwrap());
    let got = pad.received(); // asserts 65 bytes with a leading 0
    assert_eq!(got, vec![proto::bye().to_vec()]);
    assert_eq!(dev.read_packet().unwrap(), None); // nothing waiting
    pad.send(&[proto::KEY, proto::PROTO_VERSION, 4, 1]);
    assert_eq!(proto::decode(&dev.read_packet().unwrap().unwrap()), Some(proto::Event::Key { id: 4, pressed: true }));
}
