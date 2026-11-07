#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Publish a single versioned firmware asset; keep notices in the release body."""
from pathlib import Path
import argparse
import hashlib
import html
import json
import os
import shutil
import subprocess
import tempfile
from build_version import release_filename

NOTICES = ('LICENSE', 'LICENSING.md', 'CREDITS.md', 'Apache-2.0.txt',
           'BSD-2-Clause-micro-ecc.txt', 'BSP-NOTICE.txt', 'BSP-LICENSING.md',
           'AAC-NOTICE.txt', 'MBOOT-LICENSE.txt', 'MBOOT-NOTICE.md')
MARKER = '<!-- audio-router-distribution -->'


def gh(*args):
    return subprocess.check_output(['gh', *map(str, args)], text=True)


def view(tag):
    return json.loads(gh('release', 'view', tag, '--json', 'tagName,body,assets,isDraft,isPrerelease'))


def prepare(directory, tag, repo, body, output):
    info = json.loads((directory / 'manifest.json').read_text())
    firmware = directory / 'audio-router.fwsc'
    digest = hashlib.sha256(firmware.read_bytes()).hexdigest()
    if info['tag'] != tag or digest != info['firmware_sha256']:
        raise ValueError('Release firmware/manifest/tag mismatch')
    name = release_filename(tag)
    asset = output / name
    shutil.copy2(firmware, asset)
    url = f'https://github.com/{repo}'
    text = body.split(MARKER)[0].rstrip() + '\n\n' + MARKER + '\n'
    text += (f'Firmware: **{name}**\n\n'
             'Install with M-Flasher. Keep bootloader replacement disabled and '
             'settings/Bluetooth erase unchecked.\n\n'
             f'SHA-256: `{digest}`\n\n'
             f'[Matching source]({url}/archive/refs/tags/{tag}.zip) · '
             f'[Installation and recovery]({url}/blob/{tag}/docs/INSTALL.md)\n\n'
             '<details>\n<summary>Licenses and third-party notices</summary>\n\n')
    for notice in NOTICES:
        text += f'<h4>{notice}</h4>\n<pre>{html.escape((directory / notice).read_text())}</pre>\n\n'
    text += '</details>\n'
    if len(text) > 125000:
        raise ValueError('Release notes exceed GitHub body limit')
    notes = output / 'notes.md'
    notes.write_text(text)
    return asset, notes


def prune_assets(tag, filename):
    assets = view(tag)['assets']
    if not any(a['name'] == filename for a in assets):
        raise ValueError('Refusing cleanup without the versioned firmware asset')
    for asset in assets:
        if asset['name'] != filename:
            gh('release', 'delete-asset', tag, asset['name'], '--yes')


def publish(directory, tag, repo, existing):
    with tempfile.TemporaryDirectory() as temp:
        asset, notes = prepare(directory, tag, repo, existing['body'] if existing else '', Path(temp))
        if existing:
            # Preserve notices before removing any of their old attachments.
            gh('release', 'edit', tag, '--notes-file', notes)
            gh('release', 'upload', tag, asset, '--clobber')
        else:
            flags = ['--prerelease'] if '-' in tag else ['--latest']
            gh('release', 'create', tag, asset, '--verify-tag', '--title', tag,
               '--notes-file', notes, *flags)
        prune_assets(tag, asset.name)


def tidy_legacy(tag, repo):
    release = view(tag)
    names = {a['name'] for a in release['assets']}
    # Only migrate the old complete Audio Router bundle, never unrelated assets.
    expected = {'audio-router.fwsc', 'manifest.json', 'SHA256SUMS',
                'audio-router-source.tar.xz', 'audio-router-debug.tar.xz',
                'fm1-uboot.boot', 'INSTALL.txt', *NOTICES}
    if release['isDraft'] or names != expected:
        return
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        gh('release', 'download', tag, '--dir', directory)
        subprocess.run(['sha256sum', '--check', 'SHA256SUMS'], cwd=directory, check=True)
        publish(directory, tag, repo, release)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--directory', type=Path, default=Path('release'))
    parser.add_argument('--tidy-legacy', action='store_true')
    args = parser.parse_args()
    repo = os.environ['GH_REPO']
    releases = json.loads(gh('release', 'list', '--limit', '100', '--json', 'tagName'))
    existing = view(args.tag) if any(r['tagName'] == args.tag for r in releases) else None
    publish(args.directory, args.tag, repo, existing)
    if args.tidy_legacy:
        for release in releases:
            if release['tagName'] != args.tag:
                tidy_legacy(release['tagName'], repo)


if __name__ == '__main__':
    main()
