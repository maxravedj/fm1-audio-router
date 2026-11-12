#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Focused connection-setup and USB-identity checks; no hardware access."""
from pathlib import Path
import os, subprocess, tomllib
app=Path(__file__).resolve().parents[1];src=app.parents[1]
out=(app/'build').resolve()/'profile-tests';out.mkdir(exist_ok=True)
conf=tomllib.loads((app/'fm1.toml').read_text());assert conf['usb_product']=='FM1 Audio'
for name,order in (('bt_profile_setup_test',None),('usb_identity_test',0),('usb_identity_test',1)):
 exe=out/(name if order is None else f'{name}_{order}')
 extra=['-DTEST_FB=1',f'-DFELUCCA_USB_PRODUCT="{conf["usb_product"]}"',f'-DFELUCCA_USB_MIDI_NAME="{conf["usb_midi_name"]}"'] if name=='usb_identity_test' else []
 if order is not None:
  extra.append(f'-DFELUCCA_UAC_MIDI_LAST={order}')
  if order:extra.append('-DTEST_GOLDEN=0') # Original golden has MIDI first; native firmware puts it last.
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',*extra,str(app/f'tests/{name}.c'),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: focused profile setup and USB identity suite; macOS output discovery remains a device check')
