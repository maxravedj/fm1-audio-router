#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Focused Bluetooth PCM optimization proof and host timing. No device access."""
from pathlib import Path
import os, subprocess
APP=Path(__file__).resolve().parents[1]
OUT=(APP/'build').resolve()/'bt-pcm-tests'
OUT.mkdir(exist_ok=True)
cc=os.environ.get('CC','cc')
source=APP/'tests/bt_pcm_test.c'
subprocess.run([cc,'-std=c99','-O1','-g','-fsanitize=address,undefined',
    '-fno-sanitize-recover=all',str(source),'-o',str(OUT/'sanitized')],check=True)
subprocess.run([str(OUT/'sanitized')],check=True)
subprocess.run([cc,'-std=c99','-O2',str(source),'-o',str(OUT/'benchmark')],check=True)
subprocess.run([str(OUT/'benchmark'),'--bench'],check=True)
print('PASS: bit-exact PCM optimization; FM-1 CPU percentage needs device measurement')
