#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Independent FFmpeg encode/decode comparison and real production transport/FIR."""
from pathlib import Path
import os,subprocess,random
src=Path(__file__).resolve().parents[1];root=src.parent;out=root/'build/tests/aptx';out.mkdir(parents=True,exist_ok=True)
def run(*args):subprocess.run(list(map(str,args)),check=True)
exe=out/'pipeline'
run(os.environ.get('CC','cc'),'-std=c99','-O1','-g','-fwrapv','-fsanitize=address,undefined','-fno-sanitize=shift-base','-fno-sanitize-recover=all',src/'tests/bt_aptx_cpu2.c',src/'bt/br/aptx_dec/aptx_dec.c',src/'bt/br/aptx_dec/aptx_fir.c','-o',exe)
for hd in (0,1):
 for rate in (44100,48000):
  for stress in (False,True):
   codec='aptx_hd' if hd else 'aptx';fixture=out/f'{codec}-{rate}{"-stress" if stress else ""}.bin';reference=fixture.with_suffix('.reference.pcm');pcm=fixture.with_suffix('.pcm')
   if stress:
    rng=random.Random(731);samples=bytearray()
    # Silence, full-scale impulses, alternating rails and independent noise
    # exercise predictor sign changes and adaptation beyond the sine fixture.
    for i in range(rate):
     for ch in range(2):
      v=0 if i<rate//8 else (8388607 if (i+ch)&1 else -8388608) if i<rate//4 else rng.randrange(-8388608,8388608)
      samples.extend((v&0xffffff).to_bytes(3,'little'))
    raw=fixture.with_suffix('.input.pcm');raw.write_bytes(samples)
    run('ffmpeg','-v','error','-y','-f','s24le','-ar',rate,'-ac',2,'-i',raw,'-c:a',codec,'-f',codec,fixture)
   else:run('ffmpeg','-v','error','-y','-f','lavfi','-i',f'aevalsrc=0.8*sin(2*PI*997*t)|0.7*sin(2*PI*2341*t):s={rate}:d=1','-c:a',codec,'-f',codec,fixture)
   run('ffmpeg','-v','error','-y','-f',codec,'-i',fixture,'-f','s24le',reference)
   run(exe,fixture,pcm,hd,rate)
   assert pcm.read_bytes()==reference.read_bytes()[90*6:],(codec,rate,stress)
   print('PASS: 24-bit decoder PCM bit-identical to independent FFmpeg:',codec,rate,'stress' if stress else 'sine',flush=True)
