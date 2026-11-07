#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Actual menu routing, capture WAVs and badges; no device access."""
from pathlib import Path
import subprocess,math,struct,wave
from PIL import Image
app=Path(__file__).resolve().parents[1];out=(app/'build').resolve()/'usb-out-preview';out.mkdir(exist_ok=True)
tone=out/'tone.wav'
with wave.open(str(tone),'wb') as f:
 f.setparams((2,2,44100,0,'NONE','not compressed'))
 f.writeframes(b''.join(struct.pack('<hh',int(16000*math.sin(2*math.pi*440*i/44100)),int(8000*math.sin(2*math.pi*660*i/44100))) for i in range(44100*8)))
p=out/'controls.txt';p.write_text(f'''wait 800
usb_play {tone}
tap EDIT
turn SELECT 99 0
wait 30
shot usb-out-off.png
turn ALGO 1 0
wait 30
tap SAVE
wait 50
master 0
tap PLAY
wait 150
peakreset
usb_rec raw.wav
wait 500
usb_recstop
shot usb-out-loopback.png
tap GLO
turn K1 -60 0
wait 200
press FX
wait 1100
release FX
tap FX
turn SELECT -1 0
wait 30
turn K1 -60 0
wait 100
tap EDIT
turn ALGO 1 0
wait 30
tap SAVE
wait 50
usb_rec post.wav
wait 500
usb_recstop
expect peak_db_max -60
shot usb-out-loopback-post.png
turn ALGO 4 0
wait 30
tap SAVE
wait 50
shot usb-out-master-post.png
tap HOME
wait 50
shot home-out-enabled.png
tap EDIT
turn ALGO -99 0
wait 30
tap SAVE
wait 50
usb_rec off.wav
wait 200
usb_recstop
shot usb-out-disabled.png
''')
subprocess.run([app/'build/host/fm1audio_host',p,out],check=True)
def peak(name):
 with wave.open(str(out/name),'rb') as f:
  assert f.getnchannels()==2 and f.getsampwidth()==2 and f.getframerate()==44100
  d=f.readframes(f.getnframes());return max(abs(v[0]) for v in struct.iter_unpack('<h',d))
a,b,z=peak('raw.wav'),peak('post.wav'),peak('off.wav');assert a>=15990 and .247<b/a<.256 and not z,(a,b,z)
ims={name:Image.open(out/(name+'.png')).convert('RGB') for name in ['usb-out-off','usb-out-loopback','usb-out-master-post','home-out-enabled','usb-out-disabled']}
for name in ['usb-out-loopback','usb-out-master-post','home-out-enabled']:assert ims[name].getpixel((93,2))[1]>100,'OUT not green'
for name in ['usb-out-off','usb-out-disabled']:assert all(100<=c<=200 for c in ims[name].getpixel((93,2))),'OUT not gray'
assert all(c<45 for c in ims['usb-out-off'].getpixel((238,42))) and all(150<=c<=170 for c in ims['usb-out-off'].getpixel((238,208))),'scrollbar not at bottom'
print('PASS: actual USB-out menu/SAVE, OUT gray/green across pages, eight-row scrollbar, raw Loopback at zero MASTER/mute, -6 dB input plus -6 dB EQ recording gain, OFF silence')
