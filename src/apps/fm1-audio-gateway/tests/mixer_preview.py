#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""GLO mixer and CPU display setting through the actual host application."""
from pathlib import Path
import subprocess
from PIL import Image
app=Path(__file__).resolve().parents[1];out=(app/'build').resolve()/'mixer-preview';out.mkdir(exist_ok=True)
wave=(app/'build/waveform-preview/stereo.wav').resolve();assert wave.exists()
script=out/'controls.txt';script.write_text(f'''wait 800
master 4096
usb_play {wave}
wait 800
tap GLO
wait 200
shot mixer-unity.png
turn K1 -60 0
wait 200
turn K2 -120 0
wait 200
turn K3 -60 0
wait 200
master 2048
wait 800
shot mixer-trimmed.png
master 0
wait 200
shot mixer-master-off.png
tap EDIT
turn SELECT 6 0
wait 50
turn ALGO -1 0
wait 50
tap SAVE
wait 100
shot settings-cpu-off.png
tap GLO
wait 200
shot mixer-cpu-off.png
''')
subprocess.run([app/'build/host/fm1audio_host',script,out],check=True)
u=Image.open(out/'mixer-unity.png').convert('RGB');t=Image.open(out/'mixer-trimmed.png').convert('RGB');z=Image.open(out/'mixer-master-off.png').convert('RGB')
for g in range(4):
 x=20+g*55
 assert u.crop((x,220,x+34,234)).getbbox(),'missing source name below meter'
 assert u.crop((x+39,60,x+49,176)).tobytes()!=t.crop((x+39,60,x+49,176)).tobytes(),'fader did not move'
 assert t.crop((x,40,x+55,56)).getbbox(),'missing peak and gain above meter'
assert t.crop((224,60,234,176)).tobytes()!=z.crop((224,60,234,176)).tobytes(),'MASTER does not follow pot'
for y in (58,73,87,116,145,174,203):
 assert u.crop((0,y,20,y+13)).getbbox(),'missing shared left scale value'
assert u.crop((0,58,20,216)).tobytes()==t.crop((0,58,20,216)).tobytes(),'shared scale changed with faders'
c=Image.open(out/'mixer-cpu-off.png').convert('RGB');assert not c.crop((130,0,195,18)).getbbox(),'CPU text remains'
assert c.crop((195,0,240,18)).getbbox(),'battery missing'

# Actual text placement: render expected font masks into the same application
# is covered by mixer_layout_test; these frames also demonstrate post-fader level.
assert any(r>100 and g>100 and b<80 for r,g,b in u.crop((20,60,54,212)).get_flattened_data())
def active_pixels(im):
 # Count every lit segment: attenuation may remove red/yellow alone while
 # leaving the lower green segments lit. Exclude white held-peak lines.
 return sum(max(px)>80 and max(px)-min(px)>16 for px in im.crop((20,60,54,212)).get_flattened_data())
assert active_pixels(t)<active_pixels(u), 'USB meter did not follow input attenuation'
print('PASS: actual GLO mixer, independent input faders, four stereo meters/held peaks, physical MASTER follower and saved CPU-off setting')
