//! Finding and talking to the macropad's host-link interface through Linux hidraw.

use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::os::fd::{AsRawFd, OwnedFd, RawFd};
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;

use crate::protocol::{Packet, PACKET, USAGE, USAGE_PAGE};

/// The firmware's report descriptor starts with Usage Page (0xFF4E), Usage (0x58).
pub const DESCRIPTOR_PREFIX: [u8; 5] = [0x06, (USAGE_PAGE & 0xFF) as u8, (USAGE_PAGE >> 8) as u8, 0x09, USAGE];

/// A packet-oriented link to the macropad.
pub trait Device {
    /// `Ok(Some(report))`, `Ok(None)` when nothing is waiting; an error or an empty
    /// report means the device is gone.
    fn read_packet(&mut self) -> io::Result<Option<Vec<u8>>>;
    /// `Ok(false)` when the device cannot take the packet right now.
    fn write_packet(&mut self, pkt: &Packet) -> io::Result<bool>;
    fn fd(&self) -> RawFd;
    fn path(&self) -> &str;
}

/// Device nodes of every connected macropad's host-link interface (not its keyboard).
pub fn find_devices(sysfs: &Path, dev: &Path) -> Vec<String> {
    let mut nodes: Vec<(u32, String)> = fs::read_dir(sysfs)
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| {
            let name = e.file_name().into_string().ok()?;
            let n: u32 = name.strip_prefix("hidraw")?.parse().ok()?;
            let desc = fs::read(e.path().join("device/report_descriptor")).ok()?;
            desc.starts_with(&DESCRIPTOR_PREFIX).then(|| (n, dev.join(&name).to_string_lossy().into_owned()))
        })
        .collect();
    nodes.sort();
    nodes.into_iter().map(|(_, p)| p).collect()
}

pub fn find_system_devices() -> Vec<String> {
    find_devices(Path::new("/sys/class/hidraw"), Path::new("/dev"))
}

/// One open /dev/hidrawN.  Reads return one 64-byte report; writes take a 64-byte packet.
pub struct HidrawDevice {
    file: File,
    path: String,
}

impl HidrawDevice {
    pub fn open(path: &str) -> io::Result<Self> {
        let file = OpenOptions::new().read(true).write(true).custom_flags(libc::O_NONBLOCK).open(path)?;
        Ok(HidrawDevice { file, path: path.to_string() })
    }

    /// Wrap an already open, non-blocking descriptor (tests use a socket pair).
    pub fn from_fd(fd: OwnedFd, path: &str) -> Self {
        HidrawDevice { file: File::from(fd), path: path.to_string() }
    }
}

impl Device for HidrawDevice {
    fn read_packet(&mut self) -> io::Result<Option<Vec<u8>>> {
        let mut buf = [0u8; PACKET];
        match self.file.read(&mut buf) {
            Ok(n) => Ok(Some(buf[..n].to_vec())),
            Err(e) if e.kind() == io::ErrorKind::WouldBlock => Ok(None),
            Err(e) => Err(e),
        }
    }

    fn write_packet(&mut self, pkt: &Packet) -> io::Result<bool> {
        // hidraw: the first byte is the report id, 0 for a device without numbered reports
        let mut buf = [0u8; PACKET + 1];
        buf[1..].copy_from_slice(pkt);
        match self.file.write(&buf) {
            Ok(n) => Ok(n == buf.len()),
            Err(e) if e.kind() == io::ErrorKind::WouldBlock => Ok(false),
            Err(e) if e.raw_os_error() == Some(libc::ETIMEDOUT) => Ok(false), // the host gave up on this report
            Err(e) => Err(e),
        }
    }

    fn fd(&self) -> RawFd {
        self.file.as_raw_fd()
    }

    fn path(&self) -> &str {
        &self.path
    }
}
