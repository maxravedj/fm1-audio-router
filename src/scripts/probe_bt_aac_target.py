#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Standalone pi32v2 AAC resource probe; never links AAC into gateway firmware.

Compile with the platform's actual CPU/optimization flags and retain all wrapper
operations through a synthetic entry. The ELF is a diagnostic artifact, not an
installable image. SRAM fit and real-time performance require later integration
and hardware measurements.
"""
import argparse
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'src'
MODULE = SRC / 'bt/br/aac_dec'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, default=ROOT / 'tools/jieli-open')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/tests/aac-target')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Source-list pruning must not leave stale object/stack reports in this run.
    for pattern in ('[0-9][0-9]-*.o', '[0-9][0-9]-*.su', '[0-9][0-9]-*.ll'):
        for path in out.glob(pattern):
            path.unlink()
    commands = []

    def run(argv):
        argv = [str(x) for x in argv]
        commands.append(argv)
        result = subprocess.run(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.stderr:
            print(result.stderr, end='', flush=True)
        if result.returncode:
            print(result.stdout, end='')
            raise subprocess.CalledProcessError(result.returncode, argv)
        return result.stdout

    cc = args.toolchain / 'pi32v2/bin/clang'
    ld = args.toolchain / 'pi32v2/bin/ld'
    objdump = args.toolchain / 'common/bin/objdump'
    nm = args.toolchain / 'pi32v2/bin/nm'
    compiler_version = run([cc, '--version'])
    print(compiler_version, end='', flush=True)
    # Read flags/checker from build.py without executing its app CLI or builds.
    tree = ast.parse((SRC / 'tools/build.py').read_text())
    env = {'re': re, 'CONF': {}, '__builtins__': __builtins__}
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id in {'CFLAGS', 'CPU', 'FPU', 'LINE'} for t in node.targets):
            exec(compile(ast.Module(body=[node], type_ignores=[]), 'build.py', 'exec'), env)
        if isinstance(node, ast.FunctionDef) and node.name == 'mul64_near_loads':
            exec(compile(ast.Module(body=[node], type_ignores=[]), 'build.py', 'exec'), env)
    compat = out / 'compat'
    compat.mkdir(exist_ok=True)
    (compat / 'string.h').write_text('''#pragma once
#include <stddef.h>
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
''')
    flags = ['-target', 'pi32v2', *env['CFLAGS'], *env['FPU'], '-ffreestanding', '-fwrapv',
             '-fdata-sections', '-Wno-attributes', '-include', MODULE / 'pvaac/pv_fm1.h',
             '-I' + str(compat), '-I' + str(MODULE), '-I' + str(MODULE / 'pvaac')]
    layout = out / 'layout.c'
    layout.write_text('''#include "aac_dec.c"
#include "s_tdec_int_file.h"
const unsigned aac_probe_sizes[3] = {
    (sizeof(aac_dec) + AAC_DEC_ALIGNMENT - 1u) & ~(AAC_DEC_ALIGNMENT - 1u),
    sizeof(tDec_Int_File), AAC_DEC_PCM_SAMPLES * sizeof(short)
};
''')
    ir = out / 'layout.ll'
    run([cc, *flags, '-S', '-emit-llvm', layout, '-o', ir])
    line = next(x for x in ir.read_text().splitlines() if x.startswith('@aac_probe_sizes ='))
    header, state, pcm = map(int, re.findall(r'i32 (\d+)', line))
    workspace = header + state
    print(f'Target layout: wrapper {header}, decoder state {state}, workspace {workspace}, PCM {pcm} bytes', flush=True)
    harness = out / 'probe.c'
    harness.write_text('''#include "aac_dec.h"
#include <string.h>
unsigned char probe_workspace[WORKSPACE] __attribute__((aligned(8)));
unsigned char probe_au[AAC_DEC_MAX_AU_BYTES];
short probe_pcm[AAC_DEC_PCM_SAMPLES];
unsigned char probe_asc[2] = {0x12,0x10};
volatile unsigned probe_bytes = 64;
volatile int probe_status;
/* No C runtime/heap/ROM; bytewise primitives for a complete standalone link. */
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
void *memset(void *d,int c,size_t n){unsigned char *a=d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)c;return d;}
void *memmove(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;if(a<b)for(size_t i=0;i<n;i++)a[i]=b[i];else while(n){n--;a[n]=b[n];}return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]-y[i];return 0;}
void _start(void){aac_dec *d=0;aac_dec_output result;
probe_status=aac_dec_init(probe_workspace,sizeof probe_workspace,probe_asc,sizeof probe_asc,&d);
probe_status=aac_dec_decode(d,probe_au,probe_bytes,probe_pcm,AAC_DEC_PCM_SAMPLES,&result);
probe_status=aac_dec_reset(d);aac_dec_destroy(d);for(;;){} }
'''.replace('WORKSPACE', str(workspace)))
    source_names = [x.strip() for x in (MODULE / 'sources.txt').read_text().splitlines() if x.strip() and not x.startswith('#')]
    sources = [harness, MODULE / 'aac_dec.c', *(MODULE / x for x in source_names)]
    # Probe stack-usage support once. Some JieLi toolchains cannot emit it.
    stack_supported = True
    try:
        run([cc, *flags, '-fstack-usage', '-c', harness, '-o', out / 'probe.o'])
    except subprocess.CalledProcessError:
        stack_supported = False
        print('Compiler stack-usage output unavailable; use disassembly/static audit.', flush=True)
    objects = []
    irs = []
    for i, source in enumerate(sources):
        obj = out / f'{i:02d}-{source.stem}.o'
        run([cc, *flags, *(['-fstack-usage'] if stack_supported else []), '-c', source, '-o', obj])
        objects.append(obj)
        optimized_ir = obj.with_suffix('.ll')
        run([cc, *flags, '-S', '-emit-llvm', source, '-o', optimized_ir])
        irs.append(optimized_ir)
    script = out / 'probe.ld'
    script.write_text('''ENTRY(_start)
SECTIONS {
 . = 0x01c00000;
 .text : { *(.text*) }
 .rodata : { *(.rodata*) }
 .data : { *(.data*) }
 .bss (NOLOAD) : { *(.bss*) *(COMMON) }
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
''')
    elf = out / 'aac-probe.elf'
    run([ld, '--gc-sections', '-e', '_start', '-T', script, *objects, '-Map=' + str(out / 'aac-probe.map'), '-o', elf])
    dis = run([objdump, '-d', '-mcpu=' + env['CPU'], elf])
    (out / 'aac-probe.dis').write_text(dis)
    sections = run([objdump, '-h', elf])
    (out / 'sections.txt').write_text(sections)
    symbols = run([nm, '-S', '--size-sort', elf])
    (out / 'symbols.txt').write_text(symbols)
    required = {'aac_dec_workspace_size', 'aac_dec_init', 'aac_dec_decode', 'aac_dec_reset',
                'aac_dec_destroy', 'fxp_mul32_by_16', 'fxp_mul32_Q31', 'cmplx_mul32_by_16'}
    linked_names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    if required - linked_names:
        raise SystemExit('FAIL: missing reachable wrapper/out-of-line FXP functions: ' + ', '.join(sorted(required - linked_names)))
    sizes = {name: int(size, 16) for name, size in re.findall(r'^\s*\d+\s+(\.\S+)\s+([\da-fA-F]+)', sections, re.M)}
    hazards = env['mul64_near_loads'](dis)
    spec = importlib.util.spec_from_file_location('probe_romref', SRC / 'tools/romref.py')
    romref = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(romref)
    rom = romref.rom_refs(dis)  # no exceptions, including pv_log2
    stack_rows = []
    for su in out.glob('[0-9][0-9]-*.su'):
        for row in su.read_text().splitlines():
            bits = row.split('\t')
            if len(bits) == 3:
                try:
                    stack_rows.append({'function': bits[0], 'bytes': int(bits[1]), 'kind': bits[2]})
                except ValueError:
                    pass
    stack_rows.sort(key=lambda r: r['bytes'], reverse=True)
    stack_text = run([sys.executable, SRC / 'tools/stack_depth.py', out / 'aac-probe.dis', elf,
                      *[arg for ir_path in irs for arg in ('--ir', ir_path)],
                      '--fn', 'aac_dec_decode', '--json', out / 'stack.json'])
    (out / 'stack.txt').write_text(stack_text)
    print(stack_text, end='', flush=True)
    stack = json.loads((out / 'stack.json').read_text())
    source_hashes = {str(p.relative_to(MODULE)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in sorted(MODULE.rglob('*')) if p.is_file() and (p.suffix in {'.c', '.h'} or p.name == 'sources.txt')}
    summary = {'source_count': len(sources), 'source_sha256': source_hashes,
               'compiler': compiler_version.strip(), 'flags': [str(x) for x in flags], 'sections': sizes,
               'workspace_bytes': workspace, 'wrapper_bytes': header, 'decoder_state_bytes': state,
               'pcm_bytes': pcm, 'input_bytes': 2048, 'mul64_load_hazards': hazards, 'rom_address_uses': rom,
               'largest_function_frames': stack_rows[:15], 'stack_usage_supported': stack_supported,
               'entry_stack_bytes': stack['main'][0], 'entry_stack_path': stack['main'][1],
               'stack_analysis_notes': stack['notes'],
               'limitations': 'Standalone static sizing only; no gateway SRAM fit, target execution, or hardware CPU measurement. Stack estimate excludes gateway callers/interrupt nesting; unresolved analyzer notes prevent a proven bound.'}
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k != 'source_sha256'}, indent=2), flush=True)
    if hazards or rom:
        raise SystemExit('FAIL: target instruction audit requires review')
    print('PASS: standalone AAC pi32v2 compile/link/resource probe; not included in SBC firmware')


if __name__ == '__main__':
    main()
