#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Fetch pinned host build prerequisites locally. No sudo, flashing or device access."""
from pathlib import Path
import hashlib
import platform
import shutil
import subprocess
import tarfile
import urllib.request

SRC = Path(__file__).resolve().parents[1]
PROJECT = SRC.parent
DOWNLOADS = PROJECT / "downloads/m1"
TOOLS = PROJECT / "tools"
TAG = "pi32v2-23.1.5-2"
ARCHIVE = "pi32v2-llvm-23.1.5-2-linux-x64.tar.xz"
COMPILER_SHA = "cb896562418a2be93edb9bac26af4bc81a106a32974002a78e6b9fc203c93990"
ISA_SHA = "66ac37bb2ae40e5dbbd5588a6a7c2d7baf829d7f"
SDK_SHA = "ffbb2a8d925682ba6880d643a83a63ee0691ec09"
SDK_FILES = {
    "cpu/wl82/tools/uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cpu/wl82/tools/cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cpu/wl82/tools/cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
    "LICENSE": "c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4",
}


def fetch(url, path, sha):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        temp = path.with_name(path.name + ".part")
        print(f"Downloading {url}", flush=True)
        with urllib.request.urlopen(url, timeout=120) as src, temp.open("wb") as dst:
            shutil.copyfileobj(src, dst)
        if hashlib.sha256(temp.read_bytes()).hexdigest() != sha:
            raise SystemExit(f"Hash mismatch: {temp}")
        temp.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != sha:
        raise SystemExit(f"Hash mismatch: {path}; inspect it before retrying")


if platform.system() != "Linux" or platform.machine() != "x86_64":
    raise SystemExit("This setup recipe is for Linux x86_64 (the current Arch host).")
TOOLS.mkdir(parents=True, exist_ok=True)
fetch(f"https://github.com/jvitkauskas/pi32v2-llvm/releases/download/{TAG}/{ARCHIVE}",
      DOWNLOADS / ARCHIVE, COMPILER_SHA)
llvm = TOOLS / ARCHIVE.removesuffix(".tar.xz")
if not (llvm / "bin/clang").exists():
    with tarfile.open(DOWNLOADS / ARCHIVE) as archive:
        archive.extractall(TOOLS, filter="data")
isa = SRC / "reference/pi32v2-isa"
if not isa.exists():
    isa.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["git", "init", str(isa)], check=True)
    subprocess.run(["git", "-C", str(isa), "remote", "add", "origin",
                    "https://github.com/jvitkauskas/pi32v2-isa.git"], check=True)
    subprocess.run(["git", "-C", str(isa), "fetch", "--depth=1", "origin", ISA_SHA], check=True)
    subprocess.run(["git", "-C", str(isa), "checkout", "--detach", "FETCH_HEAD"], check=True)
head = subprocess.check_output(["git", "-C", str(isa), "rev-parse", "HEAD"], text=True).strip()
if head != ISA_SHA:
    raise SystemExit(f"Expected pi32v2-isa {ISA_SHA}, found {head}; existing checkout left intact")
sdk = SRC / "vendor/AC79_SDK"
for path, sha in SDK_FILES.items():
    fetch(f"https://raw.githubusercontent.com/amitv87/fw-AC79_AIoT_SDK/{SDK_SHA}/{path}", sdk / path, sha)
runtime = PROJECT / "build/m1/runtime"
runtime.mkdir(parents=True, exist_ok=True)
# The release folds compiler-rt and the __sync runtime into a single archive.
shutil.copyfile(llvm / "lib/clang/23/lib/pi32v2/libclang_rt.builtins.a",
                runtime / "libclang_rt.builtins-pi32v2.a")
# make_dropin also requires the separate runtime archive; all its symbols are already above.
subprocess.run([str(llvm / "bin/llvm-ar"), "rcs", str(runtime / "libpi32v2.a")], check=True)
dropin = TOOLS / "jieli-open"
if not dropin.exists():
    subprocess.run(["sh", str(isa / "tools/dropin/make_dropin.sh"), str(llvm / "bin"),
                    str(runtime), str(dropin)], check=True)
print(f"Ready: {dropin}\nSDK files match fm1-base's three pinned packaging hashes.")
