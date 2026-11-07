#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build and test only the changed spectrum analysis/display. No device access."""
from pathlib import Path
import argparse
import math
import os
import struct
import subprocess
import sys
import wave

APP = Path(__file__).resolve().parents[1]
SRC = APP.parents[1]


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--preview-only", action="store_true", help="Reuse the host build and check only spectrum PNGs")
args = parser.parse_args()
if not args.preview_only:
    run(sys.executable, SRC / "scripts/build_gateway.py", "--host")
OUT = (APP / "build").resolve()
if not args.preview_only:
    for name in ("spectrum_test", "spectrum_display_test", "spectrum_controls_test", "parameters_test", "navigation_test"):
        binary = OUT / "host" / name
        run(os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g", "-w",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-DPLAT_HOST", "-DFM1B_CPU2=1", "-DFM1B_UAC=1", "-DFM1B_UAC_OUT=1",
            *[f"-I{p}" for p in (SRC / "device", SRC / "include", SRC / "lib", APP, OUT / "gen")],
            APP / "tests" / f"{name}.c", *sorted((SRC / "bt/br/sbc_dec").glob("*.c")), f"-I{SRC / 'bt/br/sbc_dec'}", "-lm", "-o", binary)
        run(binary)

preview = OUT / "spectrum-preview"
preview.mkdir(exist_ok=True)
frequencies = [20 * 1000 ** ((b + .5) / 40) for b in range(40)]
with wave.open(str(preview / "bands.wav"), "wb") as wav:
    wav.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
    frames = bytearray()
    for i in range(44100):
        value = round(32767 * .015 * sum(math.sin(2 * math.pi * f * i / 44100) for f in frequencies))
        frames.extend(struct.pack("<hh", value, -value))
    wav.writeframes(frames)
script = preview / "spectrum.txt"
script.write_text("""wait 600
master 4096
tap HOME
tap HOME
turn K1 -1 0
turn K1 1 0
turn K2 -24 0
turn K3 -2 0
wait 50
shot spectrum-controls.png
turn K2 24 0
turn K3 2 0
usb_play bands.wav
wait 350
shot spectrum-active.png
wait 300
usb_play off
wait 330
shot spectrum-peaks.png
wait 1500
shot spectrum-decay.png
wait 7000
shot spectrum-silent.png
tap EDIT
wait 40
tap HOME
wait 100
shot spectrum-reentry.png
""")
run(OUT / "host/fm1audio_host", script, preview)

from PIL import Image

bar, peak = (255, 214, 41), (255, 246, 148)  # RGB565 expanded by host PNG writer
screens = {name: Image.open(preview / f"spectrum-{name}.png").convert("RGB")
           for name in ("active", "peaks", "decay", "silent", "reentry")}
for screen in screens.values():
    assert screen.crop((0, 0, 138, 20)).getbbox(), "missing CPU readout"
    assert screen.crop((28, 182, 240, 194)).getbbox(), "missing frequency labels"
    assert screen.crop((0, 36, 26, 179)).getbbox(), "missing left dB scale"
    assert screen.crop((0, 20, 240, 36)).getbbox(), "missing knob setting readouts"
    for y in range(42, 178):
        for x in range(28, 240):
            pixel = screen.getpixel((x, y))
            assert pixel in ((0, 0, 0), bar, peak), (x, y, pixel)
            if pixel != (0, 0, 0):
                b = ((x-28+1)*40-1)//212
                assert x < 28+(b+1)*212//40-1, "chart overflow or filled gap"
detached = 0
screen = screens["peaks"]
for b in range(40):
    column = [screen.getpixel((28 + b*212//40, y)) for y in range(42, 178)]
    assert column.count(peak) == 1, f"band {b} has no single-pixel peak line"
    assert bar in column, f"band {b} is empty"
    detached += column.index(peak) + 1 < column.index(bar)
assert detached >= 30, f"only {detached} held peak lines detached from the bars"
for name in ("silent", "reentry"):
    assert not screens[name].crop((28, 42, 240, 178)).getbbox(), f"stale bar/peak on {name}"
print(f"PASS: actual host previews: 40 bars/peak lines, {detached} held above decaying bars, labels/CPU, silence and fresh re-entry")
print("PASS: spectrum previews" if args.preview_only else "PASS: focused spectrum analysis/display checks; unrelated suites not rerun")
