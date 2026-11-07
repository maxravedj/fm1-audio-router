#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Focused Bluetooth/settings verification. Host models only; never accesses a device."""
from pathlib import Path
import os, subprocess, sys, math, struct, wave
APP=Path(__file__).resolve().parents[1]
SRC=APP.parents[1]
def run(*args): subprocess.run(list(map(str,args)),check=True)
run(sys.executable,SRC/'scripts/build_gateway.py','--host')
OUT=(APP/'build').resolve()/'bluetooth-tests';OUT.mkdir(exist_ok=True)
decoder=sorted((SRC/'bt/br/sbc_dec').glob('*.c'))
flags=['-std=c99','-O1','-g','-w','-fsanitize=address,undefined','-fno-sanitize-recover=all',f'-I{SRC / "bt/br/sbc_dec"}']
for name in ('bt_receiver_test','bt_settings_test','parameters_test','navigation_test','spectrum_controls_test'):
    run(os.environ.get('CC','cc'),*flags,'-DPLAT_HOST','-DFM1B_CPU2=1','-DFM1B_UAC=1','-DFM1B_UAC_OUT=1',
        *[f'-I{p}' for p in (SRC/'device',SRC/'include',SRC/'lib',APP,APP/'build/gen')],
        APP/f'tests/{name}.c',*decoder,'-lm','-o',OUT/name)
    if name!='bt_receiver_test':run(OUT/name)
cases=[(44100,2,328000,.013),(48000,2,328000,.013),(44100,2,96000,.013),
       (48000,1,160000,.013),(44100,2,256000,.003),(48000,2,256000,.001)]
for i,(rate,ch,bitrate,delay) in enumerate(cases):
    stem=OUT/f'fixture-{i}';wav=stem.with_suffix('.wav');sbc=stem.with_suffix('.sbc');pcm=stem.with_suffix('.pcm')
    with wave.open(str(wav),'wb') as f:
        f.setparams((ch,2,rate,0,'NONE','not compressed'))
        f.writeframes(b''.join(struct.pack('<'+'h'*ch,*[round(11000*math.sin(2*math.pi*(997 if channel==0 else 2341)*n/rate)) for channel in range(ch)]) for n in range(rate)))
    run('ffmpeg','-v','error','-y','-i',wav,'-c:a','sbc','-b:a',bitrate,'-sbc_delay',delay,sbc)
    run('ffmpeg','-v','error','-y','-i',sbc,*(['-af','pan=stereo|c0=c0|c1=c0'] if ch==1 else ['-ac','2']),'-f','s16le',pcm)
    run(OUT/'bt_receiver_test',sbc,pcm,*(['offline-only'] if i else (['quick'] if '--skip-drift' in sys.argv else [])))
    header=sbc.read_bytes()[:4]
    print('fixture',i,'SBC header',header.hex(),flush=True)
# Existing SSP tests also exercise the shared controller (source) path.
for name in ('bt_ssp','bt_lmp','bt_sdps','bt_avdtp'):
    run(os.environ.get('CC','cc'),*flags,SRC/f'tests/bt/{name}_test.c','-o',OUT/name)
    run(OUT/name)
script=OUT/'settings.txt'
script.write_text('''wait 600
tap EDIT
wait 40
shot settings-off.png
turn ALGO 1 0
wait 40
shot settings-pending.png
tap SAVE
wait 40
shot settings-saved.png
tap HOME
tap HOME
wait 40
shot mode2-bt-disconnected.png
''')
run(APP/'build/host/fm1audio_host',script,OUT)
print('PASS: focused Bluetooth receiver/settings suite; physical RF/audio checks still required')
