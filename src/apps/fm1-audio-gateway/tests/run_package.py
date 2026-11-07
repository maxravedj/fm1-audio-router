#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Use the built gateway package in upstream fake-flash OTA/loader tests. No device I/O."""
from pathlib import Path
import os
import subprocess
import sys

APP = Path(__file__).resolve().parents[1]
SRC = APP.parents[1]
BUILD = (APP / "build").resolve()
OUT = BUILD / "package-tests"
OUT.mkdir(parents=True, exist_ok=True)
PKG = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else SRC.parent / "dist/audio-router.fwsc"
CC = os.environ.get("CC", "cc")


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


# A deliberately different synthetic old app, large enough that cutting the fast
# transfer after 40 replies actually interrupts it (the small M1 app needs fewer).
old_app = OUT / "old_app.bin"
old_app.write_bytes(b"\xAA" * 200000)
old = OUT / "old.fwsc"
run(sys.executable, SRC / "tools/fm1pkg_make.py", old_app, BUILD / "loader/ota.bin", old,
    "--sdk", os.environ.get("AC79_SDK", SRC.parent / "tools/AC79_SDK"))
for name, source, flags, args in (
    ("ota", "ota_test.c", ["-DEXPECT_BUNDLED_MBOOT=1"], [PKG, SRC / "tests/fixtures/m-bootloader/fm1-uboot-1.2.fwsc"]),
    ("loader", "ldr_test.c", [], [old, PKG]),
    ("loader-fast", "ldr_test.c", ["-DLDR_FAST_CHUNK=4096u", "-DOTA_FRAME_MAX=4200u"], [old, PKG]),
):
    exe = OUT / name
    run(CC, "-w", *flags, "-o", exe, SRC / "tests" / source)
    run(exe, *args)
print("PASS: gateway package OTA staging, classic/fast loaders, interrupted transfer/resume and boot-head preservation (simulation)")
