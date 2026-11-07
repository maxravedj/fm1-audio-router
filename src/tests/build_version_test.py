#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exercise tags, development labels and source hashes without device/network access."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from build_version import version_info, source_build_id

with tempfile.TemporaryDirectory() as name:
    root = Path(name)
    def git(*args):
        return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()
    git('init', '-q', '-b', 'master')
    git('-c', 'user.name=Test', '-c', 'user.email=test@localhost', 'commit', '--allow-empty', '-qm', 'test')
    with patch.dict(os.environ, {}, clear=True):
        info = version_info(root)
        assert info['version'] == 'DEV-' + git('rev-parse', 'HEAD')[:8] and info['identity'] == 'FM-1_73100'
        assert not info['tag'] and info['package'] == 'fm1audio.fwsc'
        for tag in ('v1.2.3', '1.2.3', 'v1.2.3-rc.1'):
            info = version_info(root, tag)
            assert info['identity'] == 'FM-1_731010203' and info['version'] == tag.removeprefix('v')
        for tag in ('v0.1', '0.1', 'v0.1-rc.1'):
            info = version_info(root, tag)
            assert info['identity'] == 'FM-1_731000100' and info['version'] == tag.removeprefix('v')
            assert info['package'] == f"fm1audio-{tag.removeprefix('v')}.fwsc"
        git('tag', 'v0.1')
        assert version_info(root)['version'] == '0.1'
        git('tag', '-d', 'v0.1')
        git('tag', 'v1.2.3')
        assert version_info(root)['version'] == '1.2.3'
        git('-c', 'user.name=Test', '-c', 'user.email=test@localhost', 'commit', '--allow-empty', '-qm', 'next')
        assert version_info(root)['tag'] is None, 'A nearby tag must not label a development build as a release'
        for bad in ('', 'v100.1.0', 'v1.01.0', 'v1', 'v1.01', 'v1.0.', 'v1.0.0/evil', 'v1.0.0-$(oops)', 'latest'):
            try:
                version_info(root, bad)
                raise AssertionError('bad tag accepted: ' + bad)
            except SystemExit:
                pass
    with patch.dict(os.environ, {'GITHUB_REF_TYPE': 'tag', 'GITHUB_REF_NAME': 'v2.3.4'}, clear=True):
        assert version_info(root)['identity'] == 'FM-1_731020304'
    with patch.dict(os.environ, {'GITHUB_REF_TYPE': 'tag', 'GITHUB_REF_NAME': 'v0.1'}, clear=True):
        assert version_info(root)['identity'] == 'FM-1_731000100'
    dirs = ('apps/fm1-audio-gateway', 'device', 'include', 'bt/br', 'bsp/chip')
    for rel in dirs:
        (root / 'src' / rel).mkdir(parents=True)
    p = root / 'src/device/test.c';p.write_text('one')
    a = source_build_id(root, 'DEV')
    (p.parent / '._test.c').write_bytes(b'\xff')
    assert source_build_id(root, 'DEV') == a
    p.write_text('two')
    assert source_build_id(root, 'DEV') != a
    assert source_build_id(root, '1.2.3') != source_build_id(root, 'DEV')
print('PASS: explicit/exact/CI release tags, untagged DEV labels, identity bounds/injection rejection and source hash provenance')
