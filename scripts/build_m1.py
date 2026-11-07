#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build the M1 preview; never installs firmware or opens a device."""
from pathlib import Path
import os
import subprocess
import sys

SRC = Path(__file__).resolve().parents[1]
PROJECT = SRC.parent
APP = SRC / "apps/fm1-audio-gateway"
output = PROJECT / "build/m1/gateway"
output.mkdir(parents=True, exist_ok=True)
link = APP / "build"
if not link.exists() and not link.is_symlink():
    link.symlink_to(os.path.relpath(output, APP), target_is_directory=True)
env = os.environ.copy()
env.setdefault("JIELI_TOOLCHAIN", str(PROJECT / "tools/jieli-open"))
env.setdefault("AC79_SDK", str(SRC / "vendor/AC79_SDK"))
subprocess.run([sys.executable, str(SRC / "tools/build.py"), str(APP),
                "--release", "0.0.1-m1", *sys.argv[1:]], env=env, check=True)
