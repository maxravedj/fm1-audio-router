#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Actual host renderer: mute/solo boxes, gray Mixer strips and fade setting."""
from pathlib import Path
import math, struct, subprocess, wave
from PIL import Image
app=Path(__file__).resolve().parents[1]
out=(app/'build').resolve()/'mute-solo-preview';out.mkdir(exist_ok=True)
with wave.open(str(out/'stereo.wav'),'wb') as wav:
    wav.setparams((2,2,44100,0,'NONE','not compressed'))
    pcm=bytearray()
    for i in range(44100*8):
        v=int(30000*math.sin(2*math.pi*440*i/44100))
        pcm.extend(struct.pack('<hh',v,-v//2))
    wav.writeframes(pcm)
script=out/'controls.txt';script.write_text('''wait 200
master 4096
usb_play stereo.wav
tap GLO
wait 300
shot mixer-normal.png
tapkey 0
wait 40
shot mixer-usb-muted.png
tapkey 0
tapkey 12
wait 250
shot mixer-usb-solo.png
tapkey 16
wait 250
shot mixer-usb-out-solo.png
tapkey 6
wait 200
shot mixer-master-mute.png
tap PLAY
wait 200
shot mixer-master-unmute.png
tapkey 16
tapkey 12
tap EDIT
turn SELECT 100 0
wait 40
shot settings-fade.png
turn ALGO 2 0
tap SAVE
wait 60
shot settings-1s.png
''')
subprocess.run([app/'build/host/fm1audio_host',script,out],check=True)
def rgb(name):return Image.open(out/(name+'.png')).convert('RGB')
normal=rgb('mixer-normal');mute=rgb('mixer-usb-muted');solo=rgb('mixer-usb-out-solo')
for im in (normal,mute,solo):assert im.size==(240,240)
def fill(im,g,y):return im.getpixel((20+55*g+38,y+1))
assert fill(normal,0,180)==(0,0,0) and fill(mute,0,180)[0]>240
assert fill(solo,0,197)[0]>240 and fill(solo,2,197)[1]>180
assert fill(solo,1,197)==(0,0,0) and fill(solo,3,197)==(0,0,0)
def gray_bar(im,g):
    pixels=im.crop((20+55*g,60,36+55*g,212)).get_flattened_data()
    return all(max(r,g,b)-min(r,g,b)<=7 for r,g,b in pixels) # RGB565 gray quantization
assert gray_bar(mute,0) and gray_bar(solo,1) and gray_bar(solo,3)
assert max(mute.getpixel((20,208)))>70, 'muted USB IN meter lost its live signal'
for g in range(4):
 assert normal.getpixel((20+55*g+37,211))[1]>180, 'S bottom is not aligned with meters'
 assert normal.getpixel((20+55*g+37,212))==(0,0,0), 'S extends below meter bottom'
assert not gray_bar(normal,0)
assert fill(rgb('mixer-master-mute'),3,180)[0]>240
assert fill(rgb('mixer-master-unmute'),3,180)==(0,0,0)
assert rgb('settings-fade').crop((0,182,232,210)).tobytes()!=rgb('settings-1s').crop((0,182,232,210)).tobytes()
names=('mixer-normal','mixer-usb-muted','mixer-usb-solo','mixer-usb-out-solo','settings-1s')
sheet=Image.new('RGB',(240*len(names),240))
for i,name in enumerate(names):sheet.paste(rgb(name),(240*i,0))
sheet.save(out/'overview.png')
print('PASS: actual host mute/solo outline/fill boxes, gray-only Mixer cuts, PLAY synchronization and selectable 1s fade setting rendered')
