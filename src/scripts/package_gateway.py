#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify the current firmware and assemble CI/release assets, including exact sources."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from build_version import git, source_build_id

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'src'
BUILD = ROOT / 'build/gateway'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def logged(name, command):
    logs = ROOT / 'logs/ci'
    logs.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, 'AC79_SDK': os.environ.get('AC79_SDK', str(ROOT / 'tools/AC79_SDK'))}
    with (logs / name).open('w') as log:
        subprocess.run(list(map(str, command)), cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)


def main():
    info = json.loads((BUILD / 'build-info.json').read_text())
    firmware = ROOT / 'dist/audio-router.fwsc'
    if sha(firmware) != info['firmware_sha256'] or source_build_id(ROOT, info['version']) != info['build_id']:
        raise SystemExit('Firmware/source changed since the target build; rebuild before packaging')
    boot = ROOT / info['bootloader']['path']
    if sha(boot) != info['bootloader']['sha256']:
        raise SystemExit('Bundled bootloader changed since the target build; rebuild before packaging')
    logged('runtime.log', [sys.executable, SRC / 'tests/cpu2_runtime_test.py', BUILD])
    logged('rust-build.log', ['rustc', '--edition=2021', SRC / 'scripts/verify_fwsc.rs', '-o', BUILD / 'verify-fwsc'])
    logged('package.log', [BUILD / 'verify-fwsc', firmware, BUILD / 'fm1audio.bin', info['identity'], boot])
    logged('ota.log', [sys.executable, SRC / 'apps/fm1-audio-gateway/tests/run_package.py', firmware])
    # Use Git's publication boundary, never local backups, toolchains, flash dumps or generated files.
    names = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z', '--cached', '--others', '--exclude-standard']).split(b'\0')
    sources = sorted({Path(os.fsdecode(n)) for n in names if n and (ROOT / os.fsdecode(n)).is_file() and not (ROOT / os.fsdecode(n)).is_symlink()})
    if Path('src/bsp/chip/fm1.h') not in sources:
        # BSP filenames may change; its vendored C/H source must be present in Git, not just a gitlink.
        if not any(p.parts[:3] == ('src', 'bsp', 'chip') and p.suffix == '.h' for p in sources):
            raise SystemExit('BSP sources are missing from the publication tree')
    info['sources'] = {str(p): sha(ROOT / p) for p in sources}
    info['sizes'] = json.loads((BUILD / 'sizes.json').read_text())
    info['runtime_budget'] = json.loads((BUILD / 'cpu2-budget.json').read_text())
    info['working_tree_dirty'] = bool(git(ROOT, 'status', '--porcelain'))
    info['validation'] = ['linked SRAM/stacks/scratch and IRQ ownership', 'offline M-Flasher parser and decoded firmware match', 'simulated OTA/loader interruption and boot-sector preservation']
    info['device_writes_performed'] = False
    epoch = int(git(ROOT, 'show', '-s', '--format=%ct', 'HEAD') or '0')

    def normalise(entry):
        entry.uid = entry.gid = 0
        entry.uname = entry.gname = ''
        entry.mtime = epoch
        return entry

    destination = ROOT / 'dist/release'
    destination.parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix='release-') as temp:
        out = Path(temp)
        shutil.copy2(firmware, out / 'audio-router.fwsc')
        shutil.copy2(boot, out / 'fm1-uboot.boot')
        for name in ('LICENSE', 'LICENSING.md', 'CREDITS.md'):
            shutil.copy2(ROOT / name, out / name)
        for source, name in (
            ('LICENSES/Apache-2.0.txt', 'Apache-2.0.txt'),
            ('LICENSES/BSD-2-Clause-micro-ecc.txt', 'BSD-2-Clause-micro-ecc.txt'),
            ('bsp/NOTICE', 'BSP-NOTICE.txt'),
            ('bsp/LICENSING.md', 'BSP-LICENSING.md'),
            ('bt/br/aac_dec/pvaac/NOTICE', 'AAC-NOTICE.txt'),
            ('bt/br/aptx_dec/COPYING', 'LGPL-2.1.txt'),
            ('bt/br/aptx_dec/README.md', 'APTX-NOTICE.md'),
            ('boot/m-bootloader/LICENSE', 'MBOOT-LICENSE.txt'),
            ('boot/m-bootloader/NOTICE.md', 'MBOOT-NOTICE.md'),
        ):
            shutil.copy2(SRC / source, out / name)
        (out / 'manifest.json').write_text(json.dumps(info, indent=2) + '\n')
        (out / 'INSTALL.txt').write_text(
            'FM1 Audio Router ' + info['version'] + '\n\n'
            'Install audio-router.fwsc with M-Flasher. Keep bootloader replacement disabled.\n'
            'The container embeds m-bootloader ' + info['bootloader']['version'] + '; fm1-uboot.boot is the same plain binary.\n'
            'Firmware-only updates preserve the bootloader already installed on your device.\n'
            'Leave settings/Bluetooth erase unchecked to preserve settings, FX presets and bonds.\n'
            'See docs/INSTALL.md in the matching source archive for backup/recovery guidance.\n'
            'Host/package validation does not replace hardware RF or listening tests.\n')
        archive = out / 'audio-router-source.tar.xz'
        with tarfile.open(archive, 'w:xz') as tar:
            for p in sources:
                tar.add(ROOT / p, arcname='audio-router/' + str(p), recursive=False, filter=normalise)
        with tarfile.open(archive) as tar:
            for p, digest in info['sources'].items():
                assert hashlib.sha256(tar.extractfile('audio-router/' + p).read()).hexdigest() == digest, p
        with tarfile.open(out / 'audio-router-debug.tar.xz', 'w:xz') as tar:
            for name in ('fm1audio.elf', 'fm1audio.dis', 'sizes.json', 'cpu2-budget.json', 'build-info.json'):
                tar.add(BUILD / name, arcname=name, filter=normalise)
        files = sorted(out.iterdir())
        (out / 'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.name}\n' for p in files))
        destination.mkdir(exist_ok=True)
        # Replace this generated directory only after all checks/archives succeed.
        for p in destination.iterdir():
            if p.is_file():
                p.unlink()
        for p in out.iterdir():
            p.replace(destination / p.name)
    print('PASS: checked firmware, exact source archive, debug symbols, notices and checksums:', destination)


if __name__ == '__main__':
    main()
