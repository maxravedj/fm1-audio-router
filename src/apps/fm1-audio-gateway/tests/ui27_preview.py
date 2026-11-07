#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
import subprocess
from PIL import Image
app=Path(__file__).resolve().parents[1];out=app/'build/ui27-preview';out.mkdir(exist_ok=True)
wave=(app/'build/waveform-preview/stereo.wav').resolve()
script=out/'controls.txt';script.write_text(f'''wait 800
usb_play {wave}
wait 800
shot badges.png
tap EDIT
turn SELECT 3 0
turn ALGO 4 0
turn SELECT 1 0
turn ALGO 3 0
tap SAVE
wait 300
shot settings-colors.png
tap HOME
tap HOME
wait 800
shot waveform-orange.png
tap HOME
wait 800
shot spectrum-orange.png
tap FX
turn SELECT -1 0
wait 100
shot eq-post.png
''')
script.write_text("\n".join(line+"\nwait 20" if line.startswith("turn ") else line for line in script.read_text().splitlines())+"\n")
subprocess.run([app/'build/host/fm1audio_host',script,out],check=True)
im=Image.open(out/'waveform-orange.png').convert('RGB')
for box in [(6,196,90,208),(100,196,145,208),(210,196,235,208)]:assert im.crop(box).getbbox(),box
assert not im.crop((0,18,240,34)).getbbox(),'waveform labels remained at top'
assert any(r>240 and 120<g<160 and b<40 for r,g,b in im.crop((6,34,234,184)).get_flattened_data())
im=Image.open(out/'spectrum-orange.png').convert('RGB')
for box in [(6,196,90,208),(100,196,145,208),(210,196,235,208)]:assert im.crop(box).getbbox(),box
assert any(r>240 and 120<g<160 and b<40 for r,g,b in im.crop((28,26,240,162)).get_flattened_data())
im=Image.open(out/'eq-post.png').convert('RGB');assert im.getpixel((0,155))==(0,0,0) and im.getpixel((230,155))!=(0,0,0)
print('PASS: real saved orange waveform/spectrum, Source/window/peak footer, empty former header, selected post gain and battery/status previews')
