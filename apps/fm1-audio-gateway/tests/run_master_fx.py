#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Portable master FX numerical/streaming checks; no device access."""
from pathlib import Path
import os,subprocess
app=Path(__file__).resolve().parents[1];src=app.parents[1]
out=(app/'build').resolve()/'fx-tests';out.mkdir(exist_ok=True)
for source,name in ((app/'tests/master_fx_test.c','master_fx_test'),(src/'tests/fastmath_test.c','fastmath_test')):
    exe=out/name
    subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O1','-g','-ffp-contract=off',
        '-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize-recover=all',f'-I{src / "include"}',
        str(source),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
