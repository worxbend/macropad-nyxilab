"""nyxd configuration: ~/.config/nyxd/config.toml (see config.example.toml)."""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from .metrics import NET_EXCLUDE

try:
    import tomllib  # Python 3.11+
except ImportError:  # pragma: no cover - older Pythons
    try:
        import tomli as tomllib  # type: ignore[no-redef]
    except ImportError:
        tomllib = None  # type: ignore[assignment]


@dataclass
class Config:
    interval: float = 1.0           # seconds between metrics updates (the device times out after 5 s)
    device: Optional[str] = None    # a fixed /dev/hidrawN instead of auto-discovery
    disk_path: str = "/"            # the file system the DISK bar shows
    gpu: str = "auto"               # auto | amd | nvidia | off
    net_exclude: list[str] = field(default_factory=lambda: list(NET_EXCLUDE))
    keys: dict[int, str] = field(default_factory=dict)  # HOST(id) -> shell command, run on press
    text: dict[int, str] = field(default_factory=dict)  # initial text slots


def default_path() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return Path(base) / "nyxd" / "config.toml"


def load(path: Optional[Path] = None) -> Config:
    path = path or default_path()
    if not path.exists():
        return Config()
    if tomllib is None:
        raise SystemExit(f"{path}: reading TOML needs Python 3.11+ or `pip install tomli`")
    with open(path, "rb") as f:
        raw = tomllib.load(f)
    cfg = Config()
    general = raw.get("general", {})
    for name in ("interval", "device", "disk_path", "gpu", "net_exclude"):
        if name in general:
            setattr(cfg, name, general[name])
    if not 0.2 <= float(cfg.interval) <= 4.0:
        raise SystemExit(f"{path}: interval must be 0.2 .. 4 s (the macropad drops the link after 5 s)")
    if cfg.gpu not in ("auto", "amd", "nvidia", "off"):
        raise SystemExit(f"{path}: gpu must be auto, amd, nvidia or off")
    cfg.keys = {int(k): str(v) for k, v in raw.get("keys", {}).items()}
    cfg.text = {int(k): str(v) for k, v in raw.get("text", {}).items()}
    return cfg
