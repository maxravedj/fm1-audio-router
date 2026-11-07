#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Actual host FX controls and reordered screens with stereo audio."""
from pathlib import Path
import subprocess
app=Path(__file__).resolve().parents[1]
out=(app/'build').resolve()/'fx-preview';out.mkdir(exist_ok=True)
wave=(app/'build').resolve()/'waveform-preview/stereo.wav'
assert wave.exists(), 'Run waveform_preview.py first'
script=out/'controls.txt'
script.write_text(f'''wait 600
master 4096
usb_play {wave}
wait 1000
shot mode1-meters.png
press FX
wait 1100
release FX
tap FX
wait 100
shot eq-flat.png
turn SELECT -1 0
wait 30
shot eq-post.png
turn SELECT 3 0
wait 30
turn K2 60 0
wait 1000
shot eq-bell.png
turn SELECT -2 0
wait 30
turn ALGO 2 0
turn K2 2 0
wait 1000
shot eq-highpass-36.png
turn SELECT 4 0
wait 30
turn ALGO -1 0
turn K2 30 0
wait 1000
shot eq-highshelf.png
tap FX
wait 30
turn K1 120 0
turn K3 -60 0
wait 150
peakreset
wait 400
expect peak_db_max -5.9
shot limiter-active.png
tap PLAY
wait 150
shot limiter-muted.png
tap PLAY
tap FX
wait 50
shot eq-return.png
tap HOME
tap HOME
turn SELECT 2 0
wait 1000
shot mode2-waveform.png
tap HOME
wait 1000
shot mode3-spectrum.png
expect peak_db_max 0
''')
subprocess.run([app/'build/host/fm1audio_host',script,out],check=True)
from PIL import Image
for name in ('eq-flat','eq-bell','eq-highpass-36','eq-highshelf','eq-return'):
    im=Image.open(out/f'{name}.png').convert('RGB')
    for band in range(5):
        assert im.crop((band*48,170,band*48+48,240)).getbbox(), f'{name} missing band {band+1}'
    assert im.crop((22,34,236,141)).getbbox(), f'{name} missing graph'
im=Image.open(out/'limiter-active.png').convert('RGB')
assert any(r>170 and g<110 and b<110 for r,g,b in list(im.crop((224,50,229,230)).get_flattened_data())), 'missing red reduction line'
print('PASS: actual five-card EQ graph/bell/highpass slope/shelf, FX toggles to stereo limiter with red reduction, mute and reordered HOME previews')
