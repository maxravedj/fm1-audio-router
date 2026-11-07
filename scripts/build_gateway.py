#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build locally and publish the firmware at a stable path. No device access."""
from pathlib import Path
import os
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile

SRC = Path(__file__).resolve().parents[1]
PROJECT = SRC.parent
APP = SRC / "apps/fm1-audio-gateway"
VERSION = re.search(r'^#define APP_VERSION "([^"]+)"', (APP / "app.h").read_text(), re.M)[1]
output = PROJECT / f"build/{VERSION}/gateway"
output.mkdir(parents=True, exist_ok=True)
link = APP / "build"
if link.is_symlink():
    if not link.resolve().is_relative_to(PROJECT / "build"):
        raise SystemExit(f"Existing build link is outside {PROJECT / 'build'}; left unchanged")
    link.unlink()
elif link.exists():
    raise SystemExit(f"Expected {link} to be an output symlink; directory left unchanged")
link.symlink_to(os.path.relpath(output, APP), target_is_directory=True)
env = os.environ.copy()
env.setdefault("JIELI_TOOLCHAIN", str(PROJECT / "tools/jieli-open"))
env.setdefault("AC79_SDK", str(SRC / "vendor/AC79_SDK"))
subprocess.run([sys.executable, str(SRC / "tools/build.py"), str(APP),
                "--release", VERSION, *sys.argv[1:]], env=env, check=True)
if "--host" not in sys.argv[1:]:
    package = output / f"fm1audio-{VERSION}.fwsc"
    latest = PROJECT / "dist/folder/audio-router.fwsc"
    latest.parent.mkdir(parents=True, exist_ok=True)
    # Replace only after a successful build; M-Flasher never sees a partial copy.
    with tempfile.NamedTemporaryFile(dir=latest.parent, delete=False) as f:
        temporary = Path(f.name)
    try:
        shutil.copyfile(package, temporary)
        temporary.replace(latest)
    finally:
        temporary.unlink(missing_ok=True)
    digest = hashlib.sha256(latest.read_bytes()).hexdigest()
    (latest.parent / "SHA256SUMS").write_text(f"{digest}  {latest.name}\n")
    print(f"Latest firmware: {latest}")
