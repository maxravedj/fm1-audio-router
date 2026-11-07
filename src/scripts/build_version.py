# SPDX-License-Identifier: MPL-2.0
"""Shared tag validation and source identity for builds and release packaging."""
from pathlib import Path
import hashlib
import os
import re
import subprocess

TAG = re.compile(r"v?(0|[1-9][0-9]?)\.(0|[1-9][0-9]?)(?:\.(0|[1-9][0-9]?))?(-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?")


def git(root, *args):
    p = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)
    return p.stdout.strip() if p.returncode == 0 else ""


def version_info(root, tag=None):
    commit = git(root, "rev-parse", "HEAD")
    if tag is None:
        if os.environ.get("GITHUB_REF_TYPE") == "tag":
            tag = os.environ.get("GITHUB_REF_NAME", "")
        else:
            tag = git(root, "describe", "--tags", "--exact-match", "HEAD") or None
    if tag is not None:
        m = TAG.fullmatch(tag)
        if not m or len(tag) > 40:
            raise SystemExit("Release tag must be [v]MAJOR.MINOR[.PATCH][-prerelease], each number 0..99")
        version = tag.removeprefix("v")
        identity = "FM-1_731" + "".join(f"{int(n or 0):02d}" for n in m.group(1, 2, 3))
        package = f"fm1audio-{version}.fwsc"
    else:
        version = "DEV" + ("-" + commit[:8] if commit else "")
        identity, package = "FM-1_73100", "fm1audio.fwsc"
    return dict(version=version, tag=tag, identity=identity, package=package, commit=commit)


def source_build_id(root, version):
    src = Path(root) / "src"
    h = hashlib.sha1(version.encode())
    for directory in (src / "apps/fm1-audio-gateway", src / "device", src / "include", src / "bt/br", src / "bsp/chip"):
        for p in sorted(directory.rglob("*")):
            if p.suffix in (".c", ".h", ".S", ".toml", ".ld") and "build" not in p.relative_to(directory).parts and not p.name.startswith("._"):
                h.update(str(p.relative_to(directory)).encode())
                h.update(p.read_bytes())
    return f"{int.from_bytes(h.digest()[:4], 'little'):08X}"
