#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exercise release naming, notices and safe asset cleanup without GitHub writes."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import publish_gateway as release
from build_version import release_filename

assert release_filename('v0.1') == 'fm1-audio-router-v0.1.0.fwsc'
assert release_filename('v0.1.1') == 'fm1-audio-router-v0.1.1.fwsc'
assert release_filename('v0.2.0') == 'fm1-audio-router-v0.2.0.fwsc'
assert release_filename('1.2-rc.1') == 'fm1-audio-router-v1.2.0-rc.1.fwsc'
for tag in ('v1', 'v1.2.100', 'v1.2/evil', 'v01.2'):
    try:
        release_filename(tag)
        raise AssertionError(tag)
    except ValueError:
        pass

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    bundle = root / 'bundle';bundle.mkdir()
    output = root / 'out';output.mkdir()
    firmware = b'verified firmware'
    (bundle / 'audio-router.fwsc').write_bytes(firmware)
    info = dict(tag='v0.1.1', firmware_sha256=hashlib.sha256(firmware).hexdigest())
    (bundle / 'manifest.json').write_text(json.dumps(info))
    for name in release.NOTICES:
        (bundle / name).write_text(f'{name}: copyright <author> & contributors')
    asset, notes = release.prepare(bundle, 'v0.1.1', 'owner/repo', 'Changes', output)
    assert asset.name == 'fm1-audio-router-v0.1.1.fwsc' and asset.read_bytes() == firmware
    text = notes.read_text()
    assert text.startswith('Changes\n') and info['firmware_sha256'] in text
    assert '[Matching source]' not in text and 'Install with M-Flasher.' not in text
    assert '/archive/refs/tags/' not in text
    assert 'https://github.com/owner/repo/blob/v0.1.1/docs/INSTALL.md' in text
    for name in release.NOTICES:
        assert f'{name}: copyright &lt;author&gt; &amp; contributors' in text
    release.prepare(bundle, 'v0.1.1', 'owner/repo', text, output)
    assert notes.read_text() == text, 'Repeated publication must not duplicate notices'
    (bundle / 'audio-router.fwsc').write_bytes(b'changed')
    try:
        release.prepare(bundle, 'v0.1.1', 'owner/repo', '', output)
        raise AssertionError('accepted mismatched firmware')
    except ValueError:
        pass
    (bundle / 'audio-router.fwsc').write_bytes(firmware)

    state = dict(tagName='v0.1.1', body='Changes', isDraft=False, isPrerelease=False,
                 assets=[dict(name='audio-router.fwsc'), dict(name='LICENSE')])
    commands = []
    fail_upload = False
    def fake_gh(*args):
        commands.append(args)
        command = args[1]
        if command == 'view':
            return json.dumps(state)
        if command == 'edit':
            state['body'] = Path(args[4]).read_text()
        elif command == 'upload':
            if fail_upload:
                raise subprocess.CalledProcessError(1, 'gh upload')
            assert release.MARKER in state['body'], 'Notices must exist before cleanup'
            state['assets'].append(dict(name=Path(args[3]).name))
        elif command == 'delete-asset':
            assert any(a['name'] == release_filename(state['tagName']) for a in state['assets'])
            state['assets'] = [a for a in state['assets'] if a['name'] != args[3]]
        elif command == 'download':
            for p in bundle.iterdir():
                shutil.copy2(p, Path(args[4]) / p.name)
        elif command == 'create':
            assert '--latest' in args and '--verify-tag' in args
            state['body'] = Path(args[args.index('--notes-file') + 1]).read_text()
            state['assets'] = [dict(name=Path(args[3]).name)]
        elif command == 'delete':
            assert args == ('release', 'delete', 'v0.1.1', '--yes')
            state['assets'] = []
        else:
            raise AssertionError(args)
        return ''

    with patch.object(release, 'gh', fake_gh):
        release.publish(bundle, 'v0.1.1', 'owner/repo', state.copy())
        assert state['assets'] == [dict(name=asset.name)]
        assert next(i for i,c in enumerate(commands) if c[1]=='upload') < next(i for i,c in enumerate(commands) if c[1]=='delete-asset')
        state['assets'] = [dict(name='audio-router.fwsc'), dict(name='LICENSE')]
        fail_upload = True
        commands.clear()
        try:
            release.publish(bundle, 'v0.1.1', 'owner/repo', state.copy())
            raise AssertionError('upload failure ignored')
        except subprocess.CalledProcessError:
            pass
        assert not any(c[1]=='delete-asset' for c in commands)
        assert len(state['assets']) == 2
        fail_upload = False
        release.publish(bundle, 'v0.1.1', 'owner/repo', None)
        assert state['assets'] == [dict(name=asset.name)]
        commands.clear()
        release.publish(bundle, 'v0.1.1', 'owner/repo', state.copy(), recreate=True)
        assert state['assets'] == [dict(name=asset.name)]
        assert next(i for i,c in enumerate(commands) if c[1]=='delete') < next(i for i,c in enumerate(commands) if c[1]=='create')
        assert not any('--cleanup-tag' in c for c in commands)
        summary = '- aptX and aptX HD support\n- ui improvements\n'
        release.publish(bundle, 'v0.1.1', 'owner/repo', state.copy(), notes_text=summary)
        assert state['body'].startswith(summary + '\n' + release.MARKER)
        assert state['body'].count(release.MARKER) == 1
        for notice in ('LGPL-2.1.txt', 'APTX-NOTICE.md'):
            assert notice in state['body']
        commands.clear()
        (bundle / 'audio-router.fwsc').write_bytes(b'corrupt')
        try:
            release.publish(bundle, 'v0.1.1', 'owner/repo', state.copy(), recreate=True)
            raise AssertionError('replacement was not validated before deletion')
        except ValueError:
            pass
        assert not commands
        (bundle / 'audio-router.fwsc').write_bytes(firmware)
        commands.clear()
        release.tidy_legacy('v0.1.1', 'owner/repo')
        assert all(c[1]=='view' for c in commands), 'Already migrated releases must be untouched'
        state['assets'] = [dict(name='unrelated.bin')]
        commands.clear()
        release.tidy_legacy('v0.1.1', 'owner/repo')
        assert all(c[1]=='view' for c in commands), 'Unrecognized releases must be untouched'
        info['tag'] = 'v0.1'
        (bundle / 'manifest.json').write_text(json.dumps(info))
        for name in ('audio-router-source.tar.xz', 'audio-router-debug.tar.xz', 'fm1-uboot.boot', 'INSTALL.txt'):
            (bundle / name).write_bytes(b'legacy artifact')
        (bundle / 'SHA256SUMS').write_text(''.join(
            f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in sorted(bundle.iterdir())))
        state['tagName'] = 'v0.1'
        state['assets'] = [dict(name=p.name) for p in bundle.iterdir()]
        release.tidy_legacy('v0.1', 'owner/repo')
        assert state['assets'] == [dict(name='fm1-audio-router-v0.1.0.fwsc')]
        assert '/archive/refs/tags/' not in state['body']

print('PASS: normalized versioned firmware, concise notes with notices, validated release recreation, idempotent notes, one-asset publication, upload-before-delete, failed-upload preservation and bounded legacy migration')
