//! nyxd: the host bridge of the Nyxilab macropad (see host/README.md and docs/host-bridge.md).
//!
//! The daemon finds the macropad's host-link HID interface, sends it this computer's
//! metrics and text, and runs commands for its `HOST(n)` keys.  `nyxctl` talks to the
//! daemon over a Unix socket.

pub mod config;
pub mod daemon;
pub mod device;
pub mod log;
pub mod metrics;
pub mod protocol;
