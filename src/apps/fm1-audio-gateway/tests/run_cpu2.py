#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Focused two-core load sampling and actual status-bar rendering checks."""
from pathlib import Path
import os,subprocess
app=Path(__file__).resolve().parents[1];src=app.parents[1];out=(app/'build').resolve()
tests=out/'cpu2-tests';tests.mkdir(exist_ok=True)
flags=[os.environ.get('CC','cc'),'-std=c11','-O1','-g','-w','-fsanitize=address,undefined','-fno-sanitize-recover=all']
for name in ('cpu_workload_test','cpu2_panel_test'):
 cmd=[*flags,*[f'-I{p}' for p in (src/'device',src/'include')]]
 if name=='cpu2_panel_test':
  cmd+=['-DPLAT_HOST','-DFM1B_CPU2=1','-DFM1B_UAC=1','-DFM1B_UAC_OUT=1',*[f'-I{p}' for p in (src/'lib',app,out/'gen',src/'bt/br/sbc_dec')]]
 cmd+=[str(app/'tests'/f'{name}.c')]
 if name=='cpu2_panel_test':cmd+=list(map(str,(src/'bt/br/sbc_dec').glob('*.c')))+['-lm']
 exe=tests/name;subprocess.run([*cmd,'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
