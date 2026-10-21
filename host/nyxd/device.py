"""Finding and talking to the macropad's host-link interface through Linux hidraw."""
from __future__ import annotations

import errno
import os
from pathlib import Path
from typing import Optional

from .protocol import PACKET, USAGE, USAGE_PAGE

# The firmware's report descriptor starts with Usage Page (0xFF4E), Usage (0x58).
DESCRIPTOR_PREFIX = bytes([0x06, USAGE_PAGE & 0xFF, USAGE_PAGE >> 8, 0x09, USAGE])


def find_devices(sysfs: str = "/sys/class/hidraw", dev: str = "/dev") -> list[str]:
    """Device nodes of every connected macropad's host-link interface (not its keyboard)."""
    found = []
    for node in sorted(Path(sysfs).glob("hidraw*"), key=lambda p: int(p.name[6:] or 0)):
        try:
            desc = (node / "device" / "report_descriptor").read_bytes()
        except OSError:
            continue
        if desc.startswith(DESCRIPTOR_PREFIX):
            found.append(str(Path(dev) / node.name))
    return found


class HidrawDevice:
    """One open /dev/hidrawN.  Reads return one 64-byte report; writes take a 64-byte packet."""

    def __init__(self, fd: int, path: str = ""):
        self.fd, self.path = fd, path

    @classmethod
    def open(cls, path: str) -> "HidrawDevice":
        return cls(os.open(path, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC), path)

    def fileno(self) -> int:
        return self.fd

    def read(self) -> Optional[bytes]:
        """The next report, or None if there is none right now.  OSError = the device is gone."""
        try:
            return os.read(self.fd, PACKET)
        except BlockingIOError:
            return None

    def write(self, pkt: bytes) -> bool:
        """Queue a packet; False if the device's buffer is full right now."""
        assert len(pkt) == PACKET
        try:
            # hidraw: the first byte is the report id, 0 for a device without numbered reports
            return os.write(self.fd, b"\0" + pkt) == PACKET + 1
        except BlockingIOError:
            return False
        except OSError as e:
            if e.errno == errno.ETIMEDOUT:  # the host's USB stack gave up on this one report
                return False
            raise

    def close(self) -> None:
        try:
            os.close(self.fd)
        except OSError:
            pass
