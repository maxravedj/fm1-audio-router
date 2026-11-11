#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compile/link the isolated LDAC decoder for pi32v2; no firmware/device I/O.

This excludes transport, compressed/PCM queues, FIR history, platform stacks
and the rest of the firmware. Linked sizes are not a full integration budget.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import sys

SRC = Path(__file__).resolve().parents[1]
ROOT = SRC.parent
OUT = ROOT / 'build/tests/ldac/target'
OUT.mkdir(parents=True, exist_ok=True)
TC = Path(os.environ.get('JIELI_TOOLCHAIN', ROOT / 'tools/jieli-open'))
sys.path.insert(0, str(SRC / 'tools'))
import build
import stack_depth

def run(*args):
    return subprocess.check_output(list(map(str, args)), cwd=ROOT, text=True)

cc = TC / 'pi32v2/bin/clang'
flags = ['-target', 'pi32v2', '-mcpu=r3', '-mfprev1', '-O2', '-fwrapv',
         '-ffp-contract=off', '-ffunction-sections', '-fdata-sections',
         '-fno-builtin', '-Wall', '-Werror', '-Wno-unused-function',
         '-I'+str(SRC / 'apps/fm1-audio-gateway/compat')]
source = SRC / 'bt/br/ldac_dec/ldac_dec.c'
unit = OUT / 'ldac.o'
run(cc, *flags, '-c', source, '-o', unit)
irfile = OUT / 'ldac.opt.ll'
run(cc, *flags, '-S', '-emit-llvm', source, '-o', irfile)
ir = irfile.read_text()
sizefn = re.search(r'define[^\n]*@fm1_ldac_dec_size\([^}]+?\n}', ir)
assert sizefn, 'workspace size not found'
workspace = int(re.search(r'ret i32 (\d+)', sizefn[0])[1])
support = OUT / 'support.o'
run(cc, *flags, '-c', SRC / 'tests/ldac_target_probe.c', '-o', support)
script = OUT / 'probe.ld'
script.write_text('''SECTIONS {
    . = 0x01c00000;
    .text : { *(.text*) }
    .rodata : ALIGN(8) { *(.rodata*) }
    .data : ALIGN(8) { *(.data*) }
    .bss : ALIGN(8) { *(.bss*) *(COMMON) }
}
''')
elf = OUT / 'ldac.elf'
roots = ['fm1_ldac_dec_'+s for s in ('size', 'init', 'reset', 'destroy', 'frame', 'check')]
keep = [flag for name in roots for flag in ('-u', name)]
run(TC/'pi32v2/bin/ld', '--gc-sections', '-e', roots[-1], *keep,
    '-T', script, unit, support, '-o', elf)
dis = run(TC/'common/bin/objdump', '-d', '-mcpu=r3', elf)
(OUT/'ldac.dis').write_text(dis)
symbols = run(TC/'common/bin/objdump', '-t', elf)
(OUT/'symbols.txt').write_text(symbols)
assert not re.search(r'\*UND\*[ \t]+[\da-f]+[ \t]+\S', symbols), 'unresolved target symbols'
assert not re.search(r'\b(?:malloc|calloc|free|ldaclib_encode|__\w*df\w*)\b', symbols)
assert not build.mul64_near_loads(dis), 'wide multiply/load erratum risk'
assert all(name+':' in dis for name in ('ldac_fm1_mul_rsft','ldac_fm1_mul_lsft')), 'wide multiply helpers unexpectedly inlined'
sys.path.insert(0,str(SRC/'tests'))
from bt_ldac_native import check_native
check_native(dis)
sections = run(TC/'common/bin/objdump', '-h', elf)
footprint = {name:int(size, 16) for name,size in
             re.findall(r'^\s*\d+\s+(\.\w+)\s+([\da-f]+)\s+', sections, re.M)}
depth = stack_depth.analyse(dis, stack_depth.elf_words(elf), [ir])
assert not stack_depth.STRADDLE, stack_depth.STRADDLE
for key in ('recursion', 'var', 'unknown', 'indirect, all taken'):
    assert not depth['notes'].get(key), depth['notes']
frames = {name:depth['depth'](name)[0] for name in roots}
report = dict(workspace_bytes=workspace, target='pi32v2 r3', flags=flags,
              text_including_probe_libc=footprint.get('.text', 0),
              rodata=footprint.get('.rodata', 0), data=footprint.get('.data', 0),
              bss=footprint.get('.bss', 0), stack_bytes=frames,
              scope='isolated decoder; not firmware/runtime headroom')
(OUT/'budget.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
print('PASS: isolated LDAC target compile/link, bounded stacks, no heap/soft-double imports, multiply/load check')
