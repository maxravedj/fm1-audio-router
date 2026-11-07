#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
import argparse,subprocess,os
src=Path(__file__).resolve().parents[1];root=src.parent
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--fixtures',type=Path,required=True);p.add_argument('--sbc',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
mod=src/'bt/br/aac_dec';exe=a.out/'pipeline'
cmd=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-fwrapv','-fsanitize=address,undefined','-fno-sanitize=shift-base','-fno-sanitize-recover=all','-pthread','-Wno-attributes','-include',mod/'pvaac/pv_fm1.h',f'-I{mod}',f'-I{src}/bt/br/sbc_dec',src/'tests/bt_resample_cpu2.c',mod/'aac_dec.c',*[mod/p for p in (mod/'sources.txt').read_text().splitlines()],*sorted((src/'bt/br/sbc_dec').glob('*.c')),'-o',exe]
subprocess.run(list(map(str,cmd)),check=True)
for i,fixture in enumerate(sorted(a.fixtures.glob('*.rtp'))):
 sbc=a.sbc/f'{i%6}.sbc';spcm=a.out/'sbc.pcm';apcm=a.out/'aac.pcm'
 subprocess.run(list(map(str,[exe,sbc,fixture,spcm,apcm])),check=True,env={**os.environ,**({"FM1_SKIP_ORACLE":"1"} if i else {})})
 assert spcm.read_bytes()==(a.sbc/f'{i%6}-reference.pcm').read_bytes(),sbc
 assert apcm.read_bytes()==fixture.with_suffix('.pcm').read_bytes(),fixture
 print('PASS: real CPU2 resampler/decoder, frozen raw PCM match:',fixture.name,sbc.name,flush=True)
print('PASS: all marked/unmarked AAC and SBC threaded pipeline fixtures match prior decoded PCM; full-precision FIR and tap oracle pass')
