#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""OUT layout/recording visibility through actual host callbacks; no device IO."""
from pathlib import Path
import subprocess
from PIL import Image
app=Path(__file__).resolve().parents[1];out=(app/'build').resolve()/'out-monitor-preview';out.mkdir(exist_ok=True)
tone=(app/'build/usb-out-preview/tone.wav').resolve();assert tone.exists()
script=out/'controls.txt';script.write_text(f'''wait 800
master 4096
usb_play {tone}
tap EDIT
turn SELECT 99 0
turn ALGO 1 0
tap SAVE
tap HOME
usb_rec out.wav
wait 500
shot home-out.png
tap HOME
turn SELECT 2 0
turn K1 99 0
wait 500
shot waveform-out.png
tap HOME
wait 500
shot spectrum-out.png
tap GLO
wait 200
shot mixer-out.png
turn K3 -60 0
wait 500
shot mixer-out-trim.png
usb_recstop
tap HOME
tap HOME
tap HOME
wait 500
shot waveform-out-inactive.png
tap EDIT
wait 50
shot settings-right-scrollbar.png
''')
subprocess.run([app/'build/host/fm1audio_host',script,out],check=True)
ims={p.stem:Image.open(p).convert('RGB') for p in out.glob('*.png')}
blue=lambda im:sum(b>150 and b>r*2 and b>g for r,g,b in im.crop((6,34,234,184)).get_flattened_data())
assert blue(ims['waveform-out'])>400,'OUT waveform missing while recording'
assert blue(ims['waveform-out-inactive'])<=2*228+12,'inactive OUT retained old waveform'
assert ims['spectrum-out'].crop((32,24,240,184)).getbbox(),'OUT spectrum missing'
for g in range(4):assert ims['home-out'].crop((26+54*g,44,60+54*g,209)).getbbox(),'missing meter column'
a=ims['mixer-out'];b=ims['mixer-out-trim']
assert a.crop((162,60,172,212)).tobytes()!=b.crop((162,60,172,212)).tobytes(),'K3 did not move OUT fader'
for x in (0,60,180):assert a.crop((x+3,60,x+37,212)).tobytes()==b.crop((x+3,60,x+37,212)).tobytes(),'OUT trim changed other meters'
assert ims['settings-right-scrollbar'].getpixel((238,208))[0]>150,'missing right scrollbar'
print('PASS: OUT home/waveform/spectrum/Mixer previews, K3 fader isolation, inactive recording silence and right Settings scrollbar')
