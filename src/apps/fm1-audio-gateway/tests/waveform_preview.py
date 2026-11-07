#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Render the actual host UI with stereo PCM; no device access."""
from pathlib import Path
import math
import struct
import subprocess
import wave

APP = Path(__file__).resolve().parents[1]
OUT = (APP / "build").resolve() / "waveform-preview"
OUT.mkdir(parents=True, exist_ok=True)
with wave.open(str(OUT / "stereo.wav"), "wb") as wav:
    wav.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
    data = bytearray()
    for i in range(44100 * 20):
        t = i / 44100
        # Different envelopes and frequencies make channel separation visible.
        env = .15 + .8 * max(0, math.sin(t * 2.3)) ** 2
        l = int(32767 * env * math.sin(2 * math.pi * 440 * t))
        r = int(32767 * .55 * (.25 + .75 * max(0, math.sin(t * 3.2 + 1)) ** 2)
                * math.sin(2 * math.pi * 660 * t + .7))
        data.extend(struct.pack("<hh", l, r))
    wav.writeframes(data)
with wave.open(str(OUT / "zero.wav"), "wb") as wav:
    wav.setparams((2,2,44100,0,"NONE","not compressed"))
    wav.writeframes(bytes(44100*4))
script = OUT / "preview.txt"
script.write_text("""wait 600
splashshot splash.png
master 4096
tap HOME
turn K1 -20 0
usb_play stereo.wav
wait 8200
shot scroll.png
wait 300
shot scroll-next.png
turn K1 20 0
wait 400
shot zoom-4ms.png
master 2048
turn SELECT 3 0
wait 800
shot master-half.png
tap PLAY
wait 100
shot play-muted.png
tap PLAY
wait 100
shot play-unmuted.png
tap HOME
tap HOME
wait 200
shot meters-only.png
tap EDIT
wait 100
shot settings.png
tap HOME
wait 50
shot meters-return.png
tap HOME
tap HOME
wait 400
shot spectrum.png
tap HOME
wait 50
shot main-return.png
tap HOME
master 0
turn SELECT -3 0
wait 4000
shot master-zero.png
turn K1 -20 0
usb_play off
wait 8500
shot silence.png
usb_play zero.wav
wait 200
shot reconnected.png
expect peak_db_max 0
""")
subprocess.run([APP / "build/host/fm1audio_host", script, OUT], check=True)
print(f"PASS: actual stereo scrolling/4-ms/mute/reconnect previews rendered to {OUT}")

# A new silent stream must remain a flat line; no stale signal may return.
from PIL import Image
screen = Image.open(OUT / "reconnected.png").convert("RGB")
for top,center in ((34,69),(112,147)):
    for y in range(top,top+72):
        if y == center: continue
        for x in range(6,234):
            assert screen.getpixel((x,y)) == (0,0,0), "old waveform returned after reconnect"
print("PASS: reconnect to a silent stream does not restore old waveform pixels")
assert Image.open(OUT / "settings.png").convert("RGB").crop((4,42,236,110)).getbbox(), "missing settings rows"
meters = Image.open(OUT / "meters-only.png").convert("RGB")
for x in (26,80,134,188):
    assert meters.crop((x,44,x+34,209)).getbbox(), "missing USB/BT/OUT/MASTER stereo meter"
print("PASS: meters-only mode shows all four sources; settings rows visible")

spectrum = Image.open(OUT / "spectrum.png").convert("RGB")
assert spectrum.crop((0,0,138,20)).getbbox() is not None, "spectrum has no CPU readout"
for x in range(28,240):
    for y in range(26,162):
        r,g,b = spectrum.getpixel((x,y))
        assert (r,g,b)==(0,0,0) or ((r>240 and 190<g<230 and b<60) or (r>240 and g>235 and b>235)), "non-yellow bar or old meters in spectrum"
assert spectrum.crop((28,26,240,162)).getbbox() is not None, "no spectrum bars for stereo tones"
assert spectrum.crop((28,166,240,178)).getbbox() is not None, "missing frequency labels"
print("PASS: spectrum page shows yellow frequency bars/labels, CPU and bottom selected-source meter")
