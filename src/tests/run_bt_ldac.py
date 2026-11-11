#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LDAC decoder/CPU1 transport: upstream oracle and hostile input checks.

No device access. No Bluetooth interoperability or real-time CPU claims.
"""
from pathlib import Path
import hashlib
import os
import subprocess

SRC = Path(__file__).resolve().parents[1]
ROOT = SRC.parent
MODULE = SRC / 'bt/br/ldac_dec'
OUT = ROOT / 'build/tests/ldac'
OUT.mkdir(parents=True, exist_ok=True)
REFERENCE = OUT / 'upstream'
# Reconstruct the exact pinned originals without a network dependency.
for line in (MODULE / 'UPSTREAM-SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ')
    content = (MODULE / name).read_bytes()
    if name.endswith('/fixp_ldac.h'):
        content = content.replace(b'/* FM-1: consume wide multiply/round/shift before returning to memory loads. */\n', b'')
        content = content.replace(b'ldac_fm1_mul_lsft((x), (y), (n))', b'(INT32)(((INT64)(x) * (INT64)(y)) << (-(n)))')
        content = content.replace(b'ldac_fm1_mul_rsft((x), (y), (n))', b'(INT32)((((INT64)(x) * (INT64)(y)) + ((INT64)1 << ((n)-1))) >> (n))')
    if name.endswith('/struct_ldac.h'):
        content = content.replace(
            b'#include <string.h>\n#ifndef FM1_LDAC_FREESTANDING\n'
            b'/* FM-1: fixed-point decoder needs no hosted I/O, heap or math headers. */\n'
            b'#include <stdio.h>\n#include <stdlib.h>\n#include <math.h>\n#endif',
            b'#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>')
    if name.endswith('/tables_sigproc_fixp_ldac.c'):
        content = content.replace(b'#ifndef FM1_LDAC_1FS_ONLY /* FM-1: adapter rejects 88.2/96 kHz; discard their tables. */\n', b'')
        content = content.replace(b'    }\n#endif\n\n    return;\n}\n#endif /* _ENCODE_ONLY */',
                                  b'    }\n\n    return;\n}\n#endif /* _ENCODE_ONLY */')
    assert hashlib.sha256(content).hexdigest() == digest, name
    dest = OUT / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(content)

def run(*args):
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True)

cc = os.environ.get('CC', 'cc')
common = [cc, '-std=c99', '-O2', '-g', '-fwrapv', '-ffp-contract=off']
reference = OUT / 'reference'
run(*common, '-D_32BIT_FIXED_POINT', '-I'+str(REFERENCE/'inc'),
    SRC/'tests/ldac_reference.c', REFERENCE/'src/ldaclib.c', REFERENCE/'src/ldacBT.c',
    '-lm', '-o', reference)
probe = OUT / 'probe-reference'
run(*common, '-D_32BIT_FIXED_POINT', '-I'+str(REFERENCE/'inc'),
    SRC/'tests/ldac_probe_reference.c', REFERENCE/'src/ldaclib.c', REFERENCE/'src/ldacBT.c',
    '-lm', '-o', probe)
run(probe)
test = OUT / 'decoder'
run(*common, '-fsanitize=address,undefined', '-fno-sanitize=shift-base',
    '-fno-sanitize-recover=all', SRC/'tests/ldac_decoder_test.c', MODULE/'ldac_dec.c', '-o', test)
for rate in (44100, 48000):
    for quality in (0, 1, 2, 3):
        fixture = OUT / f'{rate}-{quality}.bin'
        run(reference, rate, quality, fixture)
        run(test, rate, fixture)
pipeline = OUT / 'pipeline'
run(*common, '-fsanitize=address,undefined', '-fno-sanitize=shift-base',
    '-fno-sanitize-recover=all', '-I'+str(SRC/'include'), SRC/'tests/bt_ldac_cpu2.c', MODULE/'ldac_dec.c',
    SRC/'bt/br/aptx_dec/aptx_fir.c', '-o', pipeline)
for rate in (44100, 48000):
    for quality in (0, 1, 2, 3):
        for mode in (0, 1, 2, 3):
            result = OUT / f'{rate}-{quality}-{mode}.pcm'
            run(pipeline, OUT/f'{rate}-{quality}.bin', result, rate, mode)
            assert result.read_bytes() == result.with_suffix('.pcm.actual').read_bytes(), (rate, quality, mode)
        quiet = OUT / f'{rate}-{quality}-quiet.bin'
        run(reference, rate, quality, quiet, 'quiet')
        run(test, rate, quiet)
        result = OUT / f'{rate}-{quality}-quiet.pcm'
        run(pipeline, quiet, result, rate, 0)
        assert result.read_bytes() == result.with_suffix('.pcm.actual').read_bytes(), (rate, quality, 'quiet')
print('PASS: LDAC decoder/transport/CPU1 host checks; physical RF/CPU headroom not simulated')
