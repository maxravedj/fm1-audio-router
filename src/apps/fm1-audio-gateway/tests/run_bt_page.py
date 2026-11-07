#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Incoming page register-model and peripheral pairing regressions; no device."""
from pathlib import Path
import argparse, os, subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--auth-only',action='store_true',help='run receiver session and shared SSP tests; skip unchanged page timing/LMP tests')
args=parser.parse_args()
APP=Path(__file__).resolve().parents[1]
SRC=APP.parents[1]
OUT=(APP/'build').resolve()/'bt-page-tests';OUT.mkdir(exist_ok=True)
for source in (APP/'tests/bt_page_test.c',APP/'tests/bt_link_test.c',SRC/'tests/bt/bt_ssp_test.c',SRC/'tests/bt/bt_lmp_test.c'):
    if args.auth_only and source.stem not in {'bt_link_test','bt_ssp_test'}:continue
    binary=OUT/source.stem
    decoder=sorted((SRC/'bt/br/sbc_dec').glob('*.c')) if source.stem=='bt_link_test' else []
    subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O1','-g',f'-I{SRC / "bt/br/sbc_dec"}',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',str(source),*map(str,decoder),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: receiver authentication and shared SSP checks; RF interoperability requires device retest' if args.auth_only else
      'PASS: incoming baseband and shared SSP/LMP checks; RF interoperability requires device retest')
