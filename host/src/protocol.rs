//! Host bridge protocol, the Rust side of
//! `common/firmware/lib/nyx_core/src/core/host_proto.h`.
//!
//! Every packet is 64 bytes: `[0]` type, `[1]` protocol version, `[2..63]` payload
//! (little-endian).  Keep the two in step; `tests/interop.rs` runs the firmware's
//! C++ parser against this module.

use serde::Serialize;
use unicode_normalization::{char::is_combining_mark, UnicodeNormalization};

pub const PROTO_VERSION: u8 = 1;
pub const PACKET: usize = 64;
pub const USAGE_PAGE: u16 = 0xFF4E;
pub const USAGE: u8 = 0x58;
/// The USB interface string (the udev rule matches it).
pub const INTERFACE_NAME: &str = "Nyxilab Host Link";

// host -> device
pub const HELLO: u8 = 0x01;
pub const METRICS: u8 = 0x02;
pub const TEXT: u8 = 0x03;
pub const BYE: u8 = 0x04;
// device -> host
pub const HELLO_ACK: u8 = 0x81;
pub const KEY: u8 = 0x82;
pub const STATE: u8 = 0x83;

pub const UNKNOWN: u8 = 0xFF;
pub const NO_TEMP: i16 = i16::MIN;
pub const MAX_CORES: usize = 16;
pub const TEXT_SLOTS: usize = 4;
pub const TEXT_LEN: usize = 60;
pub const HOSTNAME_LEN: usize = 31;

pub type Packet = [u8; PACKET];

/// One sample of the host's state.  `None` = could not be measured.
#[derive(Clone, Debug, Default, PartialEq, Serialize)]
pub struct Metrics {
    pub cpu_pct: Option<f64>,
    pub cores: Vec<f64>, // per-core busy %, any count
    pub mem_used_mib: u64,
    pub mem_total_mib: u64,
    pub swap_pct: Option<f64>,
    pub disk_pct: Option<f64>,
    pub cpu_temp_c: Option<f64>,
    pub load1: f64,
    pub net_rx_bps: f64, // bytes / s
    pub net_tx_bps: f64,
    pub uptime_s: f64,
    pub gpu_pct: Option<f64>,
    pub gpu_temp_c: Option<f64>,
    pub clock: Option<(u8, u8)>, // local (hour, minute)
}

/// What the macropad sends.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Event {
    HelloAck { edition: u8, n_layers: u8, proto: u8, firmware: String },
    Key { id: u8, pressed: bool },
    State { host_view: bool, base: u8, top: u8 },
}

pub fn edition_name(edition: u8) -> String {
    match edition {
        1 => "joystick".into(),
        2 => "rotary".into(),
        n => format!("edition {n}"),
    }
}

fn packet(kind: u8, payload: &[u8]) -> Packet {
    assert!(payload.len() <= PACKET - 2, "payload too long");
    let mut p = [0u8; PACKET];
    p[0] = kind;
    p[1] = PROTO_VERSION;
    p[2..2 + payload.len()].copy_from_slice(payload);
    p
}

/// What the display fonts can show: printable ASCII, accents stripped, everything else '?'.
pub fn ascii_text(text: &str, limit: usize) -> Vec<u8> {
    text.nfkd()
        .filter(|c| !is_combining_mark(*c))
        .map(|c| if (' '..='~').contains(&c) { c as u8 } else { b'?' })
        .take(limit)
        .collect()
}

fn pct(v: Option<f64>) -> u8 {
    v.map_or(UNKNOWN, |v| v.round().clamp(0.0, 100.0) as u8)
}

fn temp(v: Option<f64>) -> i16 {
    v.map_or(NO_TEMP, |v| (v * 10.0).round().clamp(-32767.0, 32767.0) as i16)
}

fn u32_sat(v: f64) -> u32 {
    v.clamp(0.0, u32::MAX as f64) as u32
}

/// Average many cores into at most `groups` bars, keeping their order.
pub fn group_cores(cores: &[f64], groups: usize) -> Vec<f64> {
    let n = cores.len();
    if n <= groups {
        return cores.to_vec();
    }
    (0..groups)
        .map(|g| {
            let part = &cores[g * n / groups..(g + 1) * n / groups];
            part.iter().sum::<f64>() / part.len() as f64
        })
        .collect()
}

pub fn hello(hostname: &str) -> Packet {
    packet(HELLO, &ascii_text(hostname, HOSTNAME_LEN))
}

pub fn metrics(m: &Metrics) -> Packet {
    let cores = group_cores(&m.cores, MAX_CORES);
    let (h, mi) = m.clock.unwrap_or((UNKNOWN, UNKNOWN));
    let mut b = Vec::with_capacity(49);
    b.push(pct(m.cpu_pct));
    b.push(cores.len() as u8);
    let mut core_bytes = [0u8; MAX_CORES];
    for (dst, c) in core_bytes.iter_mut().zip(&cores) {
        *dst = pct(Some(*c));
    }
    b.extend_from_slice(&core_bytes);
    b.extend_from_slice(&u32_sat(m.mem_used_mib as f64).to_le_bytes());
    b.extend_from_slice(&u32_sat(m.mem_total_mib as f64).to_le_bytes());
    b.push(pct(m.swap_pct));
    b.push(pct(m.disk_pct));
    b.extend_from_slice(&temp(m.cpu_temp_c).to_le_bytes());
    b.extend_from_slice(&((m.load1 * 100.0).round().clamp(0.0, 65535.0) as u16).to_le_bytes());
    b.extend_from_slice(&u32_sat(m.net_rx_bps).to_le_bytes());
    b.extend_from_slice(&u32_sat(m.net_tx_bps).to_le_bytes());
    b.extend_from_slice(&u32_sat(m.uptime_s).to_le_bytes());
    b.push(pct(m.gpu_pct));
    b.extend_from_slice(&temp(m.gpu_temp_c).to_le_bytes());
    b.push(h);
    b.push(mi);
    packet(METRICS, &b)
}

/// TEXT for slot 0..3; an empty string clears it.
pub fn text(slot: usize, value: &str) -> Option<Packet> {
    if slot >= TEXT_SLOTS {
        return None;
    }
    let mut payload = vec![slot as u8];
    payload.extend(ascii_text(value, TEXT_LEN));
    Some(packet(TEXT, &payload))
}

pub fn bye() -> Packet {
    packet(BYE, &[])
}

/// A packet from the device, or `None` if it is not one we understand.
pub fn decode(pkt: &[u8]) -> Option<Event> {
    if pkt.len() < 4 || pkt[1] != PROTO_VERSION {
        return None;
    }
    match pkt[0] {
        HELLO_ACK if pkt.len() >= 5 => {
            let name = &pkt[5..];
            let end = name.iter().position(|&b| b == 0).unwrap_or(name.len());
            Some(Event::HelloAck {
                edition: pkt[2],
                n_layers: pkt[3],
                proto: pkt[4],
                firmware: String::from_utf8_lossy(&name[..end]).into_owned(),
            })
        }
        KEY => Some(Event::Key { id: pkt[2], pressed: pkt[3] != 0 }),
        STATE if pkt.len() >= 5 => Some(Event::State { host_view: pkt[2] != 0, base: pkt[3], top: pkt[4] }),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn metrics_layout_matches_the_firmware_offsets() {
        let m = Metrics {
            cpu_pct: Some(37.4),
            cores: vec![10.0, 20.0, 30.0, 150.0],
            mem_used_mib: 6144,
            mem_total_mib: 16384,
            swap_pct: Some(5.0),
            disk_pct: None,
            cpu_temp_c: Some(45.3),
            load1: 1.23,
            net_rx_bps: 1_500_000.0,
            net_tx_bps: 2048.0,
            uptime_s: 93784.9,
            gpu_pct: None,
            gpu_temp_c: None,
            clock: Some((14, 7)),
        };
        let p = metrics(&m);
        assert_eq!(&p[..8], &[METRICS, PROTO_VERSION, 37, 4, 10, 20, 30, 100]);
        assert_eq!(&p[20..24], &6144u32.to_le_bytes());
        assert_eq!(&p[24..28], &16384u32.to_le_bytes());
        assert_eq!((p[28], p[29]), (5, UNKNOWN));
        assert_eq!(i16::from_le_bytes([p[30], p[31]]), 453);
        assert_eq!(u16::from_le_bytes([p[32], p[33]]), 123);
        assert_eq!(&p[34..38], &1_500_000u32.to_le_bytes());
        assert_eq!(&p[42..46], &93784u32.to_le_bytes());
        assert_eq!(p[46], UNKNOWN);
        assert_eq!(i16::from_le_bytes([p[47], p[48]]), NO_TEMP);
        assert_eq!((p[49], p[50]), (14, 7));
        assert!(p[51..].iter().all(|&b| b == 0));
    }

    #[test]
    fn text_is_ascii_for_the_display() {
        assert_eq!(ascii_text("héllo ✓ wörld", 60), b"hello ? world");
        assert_eq!(ascii_text("abcdef", 3), b"abc");
        let p = text(2, "ok").unwrap();
        assert_eq!(&p[..5], &[TEXT, PROTO_VERSION, 2, b'o', b'k']);
        assert!(text(4, "x").is_none());
        assert_eq!(&hello("desk")[2..7], b"desk\0");
    }

    #[test]
    fn many_cores_are_grouped() {
        let cores: Vec<f64> = (0..32).map(f64::from).collect();
        let g = group_cores(&cores, 16);
        assert_eq!(g.len(), 16);
        assert_eq!(g[0], 0.5);
        assert_eq!(g[15], 30.5);
        assert_eq!(group_cores(&[1.0, 2.0], 16), vec![1.0, 2.0]);
    }

    #[test]
    fn decode_device_packets() {
        let mut ack = [0u8; PACKET];
        ack[..5].copy_from_slice(&[HELLO_ACK, PROTO_VERSION, 2, 5, 1]);
        ack[5..9].copy_from_slice(b"fw 1");
        assert_eq!(decode(&ack), Some(Event::HelloAck { edition: 2, n_layers: 5, proto: 1, firmware: "fw 1".into() }));
        assert_eq!(decode(&[KEY, PROTO_VERSION, 7, 1]), Some(Event::Key { id: 7, pressed: true }));
        assert_eq!(decode(&[STATE, PROTO_VERSION, 1, 3, 3]), Some(Event::State { host_view: true, base: 3, top: 3 }));
        assert_eq!(decode(&[KEY, 2, 7, 1]), None); // other protocol version
        assert_eq!(decode(&[0x99, PROTO_VERSION, 0, 0]), None);
        assert_eq!(edition_name(1), "joystick");
    }
}
