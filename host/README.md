# nyxd: the host bridge

A small Linux daemon, written in Rust, that shows this computer's CPU, memory, disk,
network, GPU and clock on the Nyxilab macropad's **HOST layer**. It also runs commands
when the macropad's `HOST(n)` keys are pressed.

```bash
cargo install --path host     # nyxd + nyxctl in ~/.cargo/bin  (or: make host-install)
sudo cp host/udev/70-nyxilab-macropad.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger
mkdir -p ~/.config/systemd/user && cp host/systemd/nyxd.service ~/.config/systemd/user/
systemctl --user enable --now nyxd

nyxctl text 0 hello there     # text on the bar display
nyxctl status
```

| | |
|---|---|
| `src/protocol.rs` | the packet format; mirrors `common/firmware/lib/nyx_core/src/core/host_proto.h` |
| `src/metrics.rs` | `/proc` and `/sys` readers (CPU, cores, memory, swap, disk, load, network, temperatures, AMD GPU; `nvidia-smi` if installed) |
| `src/device.rs` | finds the macropad's host-link `hidraw` node by its report descriptor; hidraw I/O |
| `src/daemon.rs` | the `poll(2)` loop: device, control socket (`$XDG_RUNTIME_DIR/nyxd.sock`), timers, reconnects, key commands |
| `src/config.rs` | `~/.config/nyxd/config.toml` (see `config.example.toml`); unknown keys are errors |
| `src/bin/nyxd.rs`, `src/bin/nyxctl.rs` | the two programs |
| `tests/` | the daemon loop against a fake macropad, metrics against a fake `/proc` + `/sys`, discovery, and `interop.rs` (the firmware's C++ parser fed by this crate's encoder; needs g++) |

`cargo test` runs everything, and `make test-host` adds clippy and rustfmt checks.
`cargo run --release --bin nyxd -- -v` (or `make host-run`) runs it from the source
tree. Dependencies: `libc`, `serde`/`serde_json`, `toml`, `signal-hook`, and
`unicode-normalization`, which strips accents from text for the display font.

The design, the protocol and the setup are in [docs/host-bridge.md](../docs/host-bridge.md).
