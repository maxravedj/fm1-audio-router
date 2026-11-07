#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Focused real-BSP tests for the gateway's selected USB feedback mode. No device access."""
from pathlib import Path
import os
import re
import subprocess
import tomllib

APP = Path(__file__).resolve().parents[1]
SRC = APP.parents[1]
PROJECT = SRC.parent
OUT = (APP / "build").resolve() / "feedback-tests"
OUT.mkdir(parents=True, exist_ok=True)
config = tomllib.loads((APP / "fm1.toml").read_text())
feedback = int(config.get("usb_audio_out_feedback", False))
common = [os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-w",
          "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
          *[f"-I{p}" for p in (SRC / "include", SRC / "device", SRC / "bsp/chip", SRC / "bsp/boards/fm1")]]
cases = [
    ("usb-feedback", SRC / "bsp/tests/uac_test.c", [f"-DTEST_FB={feedback}"]),
    ("master-controls", APP / "tests/master_controls_test.c", [f"-DFELUCCA_UAC_OUT_FEEDBACK={feedback}"]),
    ("packet-observers", APP / "tests/packet_meter_test.c", [f"-DTEST_FB={feedback}"]),
    ("playback-quality", SRC / "tests/usb_playback_quality_test.c", [f"-DTEST_FB={feedback}"]),
]
for name, source, defines in cases:
    binary = OUT / name
    subprocess.run([*common, *defines, str(source), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print(f"PASS: focused USB playback checks with configured feedback={feedback}; unrelated suites not rerun")
