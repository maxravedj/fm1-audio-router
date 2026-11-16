#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Focused foreground RX/LCD starvation regression; no device access."""
from pathlib import Path
import os,subprocess
src=Path(__file__).resolve().parents[1];out=src.parent/'build/tests/bt-ui';out.mkdir(parents=True,exist_ok=True)
for multipoint in (0,1):
 exe=out/f'service-{multipoint}'
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O1','-g','-fsanitize=address,undefined',
  *(['-DTEST_MULTIPOINT=1'] if multipoint else []),
  '-fno-sanitize-recover=all',f'-I{src}/include',str(src/'tests/bt_ui_service.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
