#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Affected display lifetime, navigation and Bluetooth settings. No device access."""
from pathlib import Path
import os, subprocess, sys
APP=Path(__file__).resolve().parents[1]
SRC=APP.parents[1]
def run(*a):subprocess.run(list(map(str,a)),check=True)
run(sys.executable,SRC/'scripts/build_gateway.py','--host')
OUT=(APP/'build').resolve()/'display-memory-tests';OUT.mkdir(exist_ok=True)
for name in ('display_memory_test','navigation_test','display_test','spectrum_test','spectrum_display_test','spectrum_controls_test','bt_settings_test'):
    run(os.environ.get('CC','cc'),'-std=c99','-O1','-g','-w','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-DPLAT_HOST','-DFM1B_CPU2=1','-DFM1B_UAC=1','-DFM1B_UAC_OUT=1',
        *[f'-I{p}' for p in (SRC/'device',SRC/'include',SRC/'lib',SRC/'bt/br/sbc_dec',APP,APP/'build/gen')],
        APP/f'tests/{name}.c',*sorted((SRC/'bt/br/sbc_dec').glob('*.c')),'-lm','-o',OUT/name)
    run(OUT/name)
print('PASS: focused display allocation/lifetime suite')
