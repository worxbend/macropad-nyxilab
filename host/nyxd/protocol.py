"""Host bridge protocol, the Python side of common/firmware/lib/nyx_core/src/core/host_proto.h.

Every packet is 64 bytes: [0] type, [1] protocol version, [2..63] payload (little-endian).
Keep the two files in step; tests/test_interop.py runs the C++ parser against this module.
"""
from __future__ import annotations

import struct
import unicodedata
from dataclasses import dataclass, field
from typing import Optional, Sequence, Union

PROTO_VERSION = 1
PACKET = 64
USAGE_PAGE = 0xFF4E
USAGE = 0x58
INTERFACE_NAME = "Nyxilab Host Link"  # the USB interface string (udev rule, discovery)

# host -> device
HELLO, METRICS, TEXT, BYE = 0x01, 0x02, 0x03, 0x04
# device -> host
HELLO_ACK, KEY, STATE = 0x81, 0x82, 0x83

UNKNOWN = 0xFF
NO_TEMP = -32768
MAX_CORES = 16
TEXT_SLOTS = 4
TEXT_LEN = 60
HOSTNAME_LEN = 31
EDITIONS = {1: "joystick", 2: "rotary"}


@dataclass
class Metrics:
    """One sample of the host's state.  None = could not be measured."""

    cpu_pct: Optional[float] = None
    cores: Sequence[float] = field(default_factory=list)  # per-core busy %, any count
    mem_used_mib: int = 0
    mem_total_mib: int = 0
    swap_pct: Optional[float] = None
    disk_pct: Optional[float] = None
    cpu_temp_c: Optional[float] = None
    load1: float = 0.0
    net_rx_bps: float = 0.0  # bytes / s
    net_tx_bps: float = 0.0
    uptime_s: float = 0.0
    gpu_pct: Optional[float] = None
    gpu_temp_c: Optional[float] = None
    clock: Optional[tuple[int, int]] = None  # local (hour, minute)


@dataclass
class HelloAck:
    edition: int
    n_layers: int
    proto: int
    firmware: str

    @property
    def edition_name(self) -> str:
        return EDITIONS.get(self.edition, f"edition {self.edition}")


@dataclass
class Key:
    id: int
    pressed: bool


@dataclass
class State:
    host_view: bool  # the dashboard is on screen
    base: int
    top: int


Event = Union[HelloAck, Key, State]


def _packet(kind: int, payload: bytes = b"") -> bytes:
    assert len(payload) <= PACKET - 2, len(payload)
    return bytes([kind, PROTO_VERSION]) + payload.ljust(PACKET - 2, b"\0")


def ascii_text(text: str, limit: int) -> bytes:
    """What the display fonts can show: printable ASCII, accents stripped, the rest as '?'."""
    plain = unicodedata.normalize("NFKD", text)
    plain = "".join(ch for ch in plain if not unicodedata.combining(ch))
    out = bytes(b if 0x20 <= b < 0x7F else ord("?") for b in plain.encode("ascii", "replace"))
    return out[:limit]


def _pct(v: Optional[float]) -> int:
    return UNKNOWN if v is None else max(0, min(100, round(v)))


def _temp(v: Optional[float]) -> int:
    return NO_TEMP if v is None else max(-32767, min(32767, round(v * 10)))


def _u32(v: float) -> int:
    return max(0, min(0xFFFFFFFF, int(v)))


def group_cores(cores: Sequence[float], groups: int = MAX_CORES) -> list[float]:
    """Average many cores into at most `groups` bars, keeping their order."""
    n = len(cores)
    if n <= groups:
        return list(cores)
    out = []
    for g in range(groups):
        part = cores[g * n // groups:(g + 1) * n // groups]
        out.append(sum(part) / len(part))
    return out


def hello(hostname: str) -> bytes:
    return _packet(HELLO, ascii_text(hostname, HOSTNAME_LEN))


def metrics(m: Metrics) -> bytes:
    cores = group_cores(m.cores)
    h, mi = m.clock if m.clock else (UNKNOWN, UNKNOWN)
    payload = struct.pack(
        "<BB16sIIBBhHIIIBhBB",
        _pct(m.cpu_pct),
        len(cores),
        bytes(_pct(c) for c in cores).ljust(MAX_CORES, b"\0"),
        _u32(m.mem_used_mib),
        _u32(m.mem_total_mib),
        _pct(m.swap_pct),
        _pct(m.disk_pct),
        _temp(m.cpu_temp_c),
        max(0, min(0xFFFF, round(m.load1 * 100))),
        _u32(m.net_rx_bps),
        _u32(m.net_tx_bps),
        _u32(m.uptime_s),
        _pct(m.gpu_pct),
        _temp(m.gpu_temp_c),
        h,
        mi,
    )
    return _packet(METRICS, payload)


def text(slot: int, value: str) -> bytes:
    if not 0 <= slot < TEXT_SLOTS:
        raise ValueError(f"text slot must be 0..{TEXT_SLOTS - 1}")
    return _packet(TEXT, bytes([slot]) + ascii_text(value, TEXT_LEN))


def bye() -> bytes:
    return _packet(BYE)


def decode(pkt: bytes) -> Optional[Event]:
    """A packet from the device, or None if it is not one we understand."""
    if len(pkt) < 4 or pkt[1] != PROTO_VERSION:
        return None
    kind = pkt[0]
    if kind == HELLO_ACK:
        name = pkt[5:].split(b"\0", 1)[0].decode("ascii", "replace")
        return HelloAck(pkt[2], pkt[3], pkt[4], name)
    if kind == KEY:
        return Key(pkt[2], bool(pkt[3]))
    if kind == STATE and len(pkt) >= 5:
        return State(bool(pkt[2]), pkt[3], pkt[4])
    return None
