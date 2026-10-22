//! Test helpers: temporary directories and a fake macropad.
#![allow(dead_code)]

use std::os::fd::{FromRawFd, OwnedFd};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};

use nyxd::device::HidrawDevice;
use nyxd::protocol::PACKET;

pub struct TempDir(pub PathBuf);

impl TempDir {
    pub fn new(tag: &str) -> Self {
        static N: AtomicU32 = AtomicU32::new(0);
        let p = std::env::temp_dir().join(format!(
            "nyxd-{tag}-{}-{}",
            std::process::id(),
            N.fetch_add(1, Ordering::Relaxed)
        ));
        std::fs::create_dir_all(&p).unwrap();
        TempDir(p)
    }

    pub fn write(&self, rel: &str, content: impl AsRef<[u8]>) {
        let p = self.0.join(rel);
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(p, content).unwrap();
    }

    pub fn path(&self) -> &Path {
        &self.0
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

/// The device end of a SOCK_SEQPACKET pair: like hidraw, one packet per read, and
/// end-of-file when the other side closes.  The daemon gets the other end wrapped
/// in its real HidrawDevice, so the report-id prefix it writes is checked too.
pub struct FakeMacropad {
    ours: Option<OwnedFd>,
    theirs: Option<OwnedFd>,
}

fn seqpacket_pair() -> (OwnedFd, OwnedFd) {
    let mut fds = [0; 2];
    let r = unsafe { libc::socketpair(libc::AF_UNIX, libc::SOCK_SEQPACKET | libc::SOCK_CLOEXEC, 0, fds.as_mut_ptr()) };
    assert_eq!(r, 0, "socketpair");
    unsafe { (OwnedFd::from_raw_fd(fds[0]), OwnedFd::from_raw_fd(fds[1])) }
}

impl FakeMacropad {
    pub fn new() -> Self {
        let (ours, theirs) = seqpacket_pair();
        for fd in [&ours, &theirs] {
            use std::os::fd::AsRawFd;
            let flags = unsafe { libc::fcntl(fd.as_raw_fd(), libc::F_GETFL) };
            assert_eq!(unsafe { libc::fcntl(fd.as_raw_fd(), libc::F_SETFL, flags | libc::O_NONBLOCK) }, 0);
        }
        FakeMacropad { ours: Some(ours), theirs: Some(theirs) }
    }

    /// What the daemon opens.
    pub fn device(&self, path: &str) -> HidrawDevice {
        HidrawDevice::from_fd(self.theirs.as_ref().unwrap().try_clone().unwrap(), path)
    }

    fn ours(&self) -> i32 {
        use std::os::fd::AsRawFd;
        self.ours.as_ref().unwrap().as_raw_fd()
    }

    /// Everything the daemon wrote since the last call, without the report-id byte.
    pub fn received(&self) -> Vec<Vec<u8>> {
        let mut out = Vec::new();
        let mut buf = [0u8; 128];
        loop {
            let n = unsafe { libc::recv(self.ours(), buf.as_mut_ptr().cast(), buf.len(), 0) };
            if n <= 0 {
                return out;
            }
            let n = n as usize;
            assert_eq!(n, PACKET + 1, "hidraw writes are 65 bytes");
            assert_eq!(buf[0], 0, "report id 0 first");
            out.push(buf[1..n].to_vec());
        }
    }

    pub fn kinds(&self) -> Vec<u8> {
        self.received().iter().map(|p| p[0]).collect()
    }

    pub fn send(&self, pkt: &[u8]) {
        let mut p = [0u8; PACKET];
        p[..pkt.len()].copy_from_slice(pkt);
        let n = unsafe { libc::send(self.ours(), p.as_ptr().cast(), p.len(), 0) };
        assert_eq!(n as usize, PACKET);
    }

    /// The macropad goes away: the daemon's next read sees end-of-file.
    pub fn unplug(&mut self) {
        self.ours = None;
        self.theirs = None;
    }
}
