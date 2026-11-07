#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build Audio Router at stable paths. Version releases with Git tags; no device I/O."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import tomllib
from build_version import version_info, source_build_id

SRC = Path(__file__).resolve().parents[1]
PROJECT = SRC.parent
APP = SRC / "apps/fm1-audio-gateway"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", action="store_true", help="build the Linux simulator only")
    parser.add_argument("--tag", help="explicit release tag (also works in a source archive)")
    args = parser.parse_args()
    info = version_info(PROJECT, args.tag)
    config = tomllib.loads((APP / "fm1.toml").read_text())
    sys.path.insert(0, str(SRC / "tools"))
    from fm1pkg_make import read_bootloader
    boot_path = (APP / config["bootloader"]).resolve()
    boot = read_bootloader(boot_path, config["bootloader_sha256"])
    info["bootloader"] = dict(name="m-bootloader", board="fm1",
        version=boot[16+0x50:16+0x70].split(b"\0", 1)[0].decode("ascii"),
        path=str(boot_path.relative_to(PROJECT)), sha256=hashlib.sha256(boot).hexdigest())
    output = PROJECT / "build/gateway"
    output.mkdir(parents=True, exist_ok=True)
    link = APP / "build"
    if link.is_symlink():
        if not link.resolve().is_relative_to(PROJECT / "build"):
            raise SystemExit(f"Build link is outside {PROJECT / 'build'}; left unchanged")
        link.unlink()
    elif link.exists():
        raise SystemExit(f"Expected {link} to be an output symlink; directory left unchanged")
    link.symlink_to(os.path.relpath(output, APP), target_is_directory=True)
    env = os.environ.copy()
    env.setdefault("JIELI_TOOLCHAIN", str(PROJECT / "tools/jieli-open"))
    env.setdefault("AC79_SDK", str(PROJECT / "tools/AC79_SDK"))
    env.setdefault("FM1B_OPT_IR", "1")
    flags = ["--release", info["version"]] if info["tag"] else ["--version-label", info["version"]]
    subprocess.run([sys.executable, str(SRC / "tools/build.py"), str(APP),
                    *flags, *(["--host"] if args.host else [])], env=env, check=True)
    info["build_id"] = source_build_id(PROJECT, info["version"])
    if args.host:
        (output / "host/build-info.json").write_text(json.dumps(info, indent=2) + "\n")
        return
    package = output / info["package"]
    info["firmware_sha256"] = hashlib.sha256(package.read_bytes()).hexdigest()
    (output / "build-info.json").write_text(json.dumps(info, indent=2) + "\n")
    latest = PROJECT / "dist/audio-router.fwsc"
    latest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=latest.parent, delete=False) as f:
        temporary = Path(f.name)
    try:
        shutil.copyfile(package, temporary)
        temporary.replace(latest)
    finally:
        temporary.unlink(missing_ok=True)
    (latest.parent / "SHA256SUMS").write_text(f"{info['firmware_sha256']}  {latest.name}\n")
    print(f"Latest firmware: {latest}")


if __name__ == "__main__":
    main()
