#!/usr/bin/env python3
"""Run hardware/control regressions with g++ and Python, without board dependencies."""
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
LIB = REPO / "common/firmware/lib"

with tempfile.TemporaryDirectory(prefix="nyx-hardware-tests-") as tmp:
    for edition in ("joystick", "rotary"):
        fw = REPO / edition / "firmware"
        sources = [HERE / "test_hardware.cpp", LIB / "nyx_core/src/core/engine.cpp",
                   LIB / "nyx_hw/src/hw/settings.cpp", LIB / "nyx_hw/src/hw/usb_hid.cpp",
                   fw / "src" / ("stick.cpp" if edition == "joystick" else "knob.cpp"), fw / "src/keymap.cpp"]
        if edition == "joystick":
            sources.append(fw / "src/joystick.cpp")
        includes = [HERE / "shims", fw / "include", fw / "src"]
        includes += [LIB / name / "src" for name in ("nyx_core", "nyx_hw", "nyx_ui", "nyx_app")]
        exe = Path(tmp) / edition
        command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   "-Wno-missing-field-initializers"]
        if edition == "joystick":
            command.append("-DTEST_JOYSTICK")
        command += [f"-I{path}" for path in includes]
        command += [str(path) for path in sources] + ["-o", str(exe)]
        subprocess.run(command, check=True)
        print(edition, flush=True)
        subprocess.run([str(exe)], check=True)
