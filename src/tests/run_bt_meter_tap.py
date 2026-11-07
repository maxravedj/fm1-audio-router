#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Only the changed CPU0 consumer of CPU2's prepared PCM; no device access."""
from pathlib import Path
import argparse,os,subprocess
src=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
mod=src/'bt/br/aac_dec';exe=a.out/'bt-meter-cpu2'
cmd=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-fwrapv','-ffp-contract=off',
     '-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize=shift-base','-fno-sanitize-recover=all',
     '-pthread','-Wno-attributes','-include',mod/'pvaac/pv_fm1.h',f'-I{mod}',f'-I{src}/bt/br/sbc_dec',
     src/'apps/fm1-audio-gateway/tests/bt_meter_cpu2_test.c',mod/'aac_dec.c',
     *[mod/name for name in (mod/'sources.txt').read_text().splitlines()],*sorted((src/'bt/br/sbc_dec').glob('*.c')),'-o',exe]
subprocess.run(list(map(str,cmd)),check=True);subprocess.run([exe],check=True)
