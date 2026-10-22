#!/usr/bin/env python3
"""Run hardware/control regressions with g++ and Python, without board dependencies.

    run.py              the hardware regressions (mocked Arduino, TinyUSB and EEPROM)
    run.py --sanitize   the same plus the Unity suites (core, joystick, rotary), all built with
                        AddressSanitizer + UndefinedBehaviorSanitizer; any finding fails the run.
                        Needs Unity, which `pio test -e native` downloads (make test-core).
"""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
LIB = REPO / "common/firmware/lib"
SANITIZE = "--sanitize" in sys.argv[1:]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-g", "-O1"]


def build_and_run(name: str, command: list[str], exe: Path) -> None:
    subprocess.run(command + (SAN_FLAGS if SANITIZE else []) + ["-o", str(exe)], check=True)
    print(name, flush=True)
    subprocess.run([str(exe)], check=True)


def unity_suites(tmp: Path) -> None:
    unity = sorted(REPO.glob("*/firmware/.pio/libdeps/native/Unity/src"))
    if not unity:
        sys.exit("Unity not found: run `make test-core` (pio test -e native) once first")
    core = LIB / "nyx_core/src"
    base = ["g++", "-std=gnu++17", "-Wall", "-Wextra", f"-I{core}", f"-I{unity[0]}", str(unity[0] / "unity.c")]
    build_and_run("core (Unity)", base + [str(REPO / "common/firmware/test/test_core/test_main.cpp")]
                  + [str(p) for p in sorted((core / "core").glob("*.cpp"))], tmp / "core")
    joy = REPO / "joystick/firmware"
    build_and_run("joystick (Unity)", base + [f"-I{joy / 'src'}", str(joy / "test/test_joystick/test_main.cpp"),
                                             str(joy / "src/joystick.cpp")], tmp / "joy")
    rot = REPO / "rotary/firmware"
    build_and_run("rotary (Unity)", base + [f"-I{rot / 'src'}", str(rot / "test/test_encoder/test_main.cpp")],
                  tmp / "rot")


with tempfile.TemporaryDirectory(prefix="nyx-hardware-tests-") as tmp:
    for edition in ("joystick", "rotary"):
        fw = REPO / edition / "firmware"
        sources = [HERE / "test_hardware.cpp", LIB / "nyx_core/src/core/engine.cpp",
                   LIB / "nyx_hw/src/hw/settings.cpp", LIB / "nyx_hw/src/hw/usb_hid.cpp",
                   LIB / "nyx_hw/src/hw/host_hid.cpp", LIB / "nyx_core/src/core/host_proto.cpp",
                   fw / "src" / ("stick.cpp" if edition == "joystick" else "knob.cpp"), fw / "src/keymap.cpp"]
        if edition == "joystick":
            sources.append(fw / "src/joystick.cpp")
        includes = [HERE / "shims", fw / "include", fw / "src"]
        includes += [LIB / name / "src" for name in ("nyx_core", "nyx_hw", "nyx_ui", "nyx_app")]
        command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                   "-Wno-missing-field-initializers"]
        if edition == "joystick":
            command.append("-DTEST_JOYSTICK")
        command += [f"-I{path}" for path in includes]
        command += [str(path) for path in sources]
        build_and_run(edition, command, Path(tmp) / edition)
    if SANITIZE:
        unity_suites(Path(tmp))
