# nyxd: the host bridge

A small Linux daemon that shows this computer's CPU, memory, disk, network, GPU and
clock on the Nyxilab macropad's **HOST layer**. It also runs commands when the
macropad's `HOST(n)` keys are pressed. Python 3.9+, standard library only.

```bash
pipx install ./host
sudo cp host/udev/70-nyxilab-macropad.rules /etc/udev/rules.d/ && sudo udevadm control --reload && sudo udevadm trigger
mkdir -p ~/.config/systemd/user && cp host/systemd/nyxd.service ~/.config/systemd/user/
systemctl --user enable --now nyxd

nyxctl text 0 hello there     # text on the bar display
nyxctl status
```

| | |
|---|---|
| `nyxd/protocol.py` | the packet format; mirrors `common/firmware/lib/nyx_core/src/core/host_proto.h` |
| `nyxd/metrics.py` | `/proc` and `/sys` readers (CPU, cores, memory, swap, disk, load, network, temperatures, AMD GPU; `nvidia-smi` if installed) |
| `nyxd/device.py` | finds the macropad's host-link `hidraw` node by its report descriptor |
| `nyxd/daemon.py` | the loop: device, control socket (`$XDG_RUNTIME_DIR/nyxd.sock`), timers, reconnects |
| `nyxd/cli.py` | `nyxd` and `nyxctl` |
| `config.example.toml` | copy to `~/.config/nyxd/config.toml` |
| `tests/` | `python3 -m unittest discover -s tests -t .` (or `make test-host` at the top) |

Run from the source tree without installing: `cd host && python3 -m nyxd -v`.

The design, the protocol and the setup are in [docs/host-bridge.md](../docs/host-bridge.md).
