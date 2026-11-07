#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Independent SBC fixtures, actual CPU2 worker and exact original PCM comparison."""
from pathlib import Path
import argparse,os,subprocess
src=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);out=p.parse_args().out;out.mkdir(parents=True,exist_ok=True)
def run(args):subprocess.run(list(map(str,args)),check=True)
flags=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-pthread',f'-I{src}/bt/br/sbc_dec']
for mode in ('worker','reference'):
 run([*flags,*(['-DTEST_SYNC'] if mode=='reference' else []),src/'tests/bt_sbc_cpu2.c',*sorted((src/'bt/br/sbc_dec').glob('*.c')),'-o',out/mode])
for i,(rate,ch,bitrate,delay) in enumerate(((44100,2,328000,.013),(48000,2,328000,.013),(44100,2,96000,.013),(48000,1,160000,.013),(44100,2,256000,.003),(48000,2,256000,.001))):
 fixture=out/f'{i}.sbc'
 run(['ffmpeg','-v','error','-y','-f','lavfi','-i',f'aevalsrc=0.3*sin(2*PI*997*t){"|0.2*sin(2*PI*2341*t)" if ch==2 else ""}:s={rate}:d=1','-c:a','sbc','-b:a',bitrate,'-sbc_delay',delay,fixture])
 for mode in ('worker','reference'):run([out/mode,fixture,out/f'{i}-{mode}.pcm'])
 assert (out/f'{i}-worker.pcm').read_bytes()==(out/f'{i}-reference.pcm').read_bytes()
 print('PASS: SBC worker PCM bit-exact:',rate,ch,bitrate,delay,flush=True)
print('PASS: 6 independent SBC fixtures; CPU2 PCM bit-exact, sanitizer and lifecycle checks')
