"""hidraw discovery and I/O, with a fake sysfs and a pipe."""
import os
import tempfile
import unittest
from pathlib import Path

from nyxd import protocol as proto
from nyxd.device import HidrawDevice, find_devices

KEYBOARD_DESC = bytes([0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01])
HOST_LINK_DESC = bytes([0x06, 0x4E, 0xFF, 0x09, 0x58, 0xA1, 0x01])
OTHER_VENDOR_DESC = bytes([0x06, 0x60, 0xFF, 0x09, 0x61, 0xA1, 0x01])  # e.g. a QMK raw HID keyboard


class Discovery(unittest.TestCase):
    def test_only_the_host_link_interface_matches(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name, desc in (("hidraw0", KEYBOARD_DESC), ("hidraw11", HOST_LINK_DESC),
                               ("hidraw2", OTHER_VENDOR_DESC), ("hidraw3", HOST_LINK_DESC)):
                d = Path(tmp) / name / "device"
                d.mkdir(parents=True)
                (d / "report_descriptor").write_bytes(desc)
            (Path(tmp) / "hidraw9").mkdir()  # no descriptor readable
            self.assertEqual(find_devices(tmp, "/dev"), ["/dev/hidraw3", "/dev/hidraw11"])

    def test_descriptor_matches_the_firmware(self):
        src = (Path(__file__).resolve().parents[2] / "common/firmware/lib/nyx_hw/src/hw/host_hid.cpp").read_text()
        self.assertIn("HOST_USAGE_PAGE", src)  # the firmware builds its descriptor from these constants
        self.assertEqual((proto.USAGE_PAGE, proto.USAGE), (0xFF4E, 0x58))
        hdr = (Path(__file__).resolve().parents[2] / "common/firmware/lib/nyx_core/src/core/host_proto.h").read_text()
        self.assertIn("HOST_USAGE_PAGE = 0xFF4E, HOST_USAGE = 0x58", hdr)
        self.assertIn(f"PROTO_VERSION = {proto.PROTO_VERSION};", hdr)
        self.assertIn(f'"{proto.INTERFACE_NAME}"', src)


class Io(unittest.TestCase):
    def test_write_prefixes_report_id_zero(self):
        r, w = os.pipe()
        try:
            HidrawDevice(w).write(proto.bye())
            data = os.read(r, 100)
            self.assertEqual(len(data), 65)
            self.assertEqual(data[0], 0)
            self.assertEqual(data[1:], proto.bye())
        finally:
            os.close(r)
            os.close(w)

    def test_read_without_data_returns_none(self):
        r, w = os.pipe()
        os.set_blocking(r, False)
        try:
            self.assertIsNone(HidrawDevice(r).read())
        finally:
            os.close(r)
            os.close(w)


if __name__ == "__main__":
    unittest.main()
