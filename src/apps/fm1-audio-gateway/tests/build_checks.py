# SPDX-License-Identifier: MPL-2.0
"""Regression checks for metadata handling without weakening the MMIO guard."""
from pathlib import Path
import importlib.util
import sys
import tempfile

SRC = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SRC / "tools"))
spec = importlib.util.spec_from_file_location("gateway_build", SRC / "tools/build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)

with tempfile.TemporaryDirectory() as name:
    root = Path(name)
    build.ROOT = build.SRC = root
    build.APP = root / "app"
    build.BSP = root / "bsp"
    build.CONF = {}
    build.WIFI_OWN = build.LINK = False
    for d in ("app", "device", "include", "lib", "bsp/chip", "bsp/boards/fm1"):
        (root / d).mkdir(parents=True, exist_ok=True)
    # Binary AppleDouble siblings must neither crash decoding nor become source.
    (root / "device/._main.c").write_bytes(b"\x00\xff\xfe")
    (root / "bsp/chip/._regs.h").write_bytes(b"\x00\xff\xfe")
    (root / "app/app.c").write_text("void safe(void) {}\n")
    assert build.mmio_check() == []
    # Real application accesses are still refused, including indirect HAL macros.
    (root / "bsp/chip/regs.h").write_text("#define SECRET (*(volatile unsigned *)0x10000u)\n")
    (root / "app/app.c").write_text("unsigned bad(void) { return SECRET; }\n")
    assert any("BSP register macro SECRET" in e for e in build.mmio_check())
    (root / "app/app.c").write_text("unsigned bad(void) { return *(volatile unsigned *)0x10000u; }\n")
    errors = build.mmio_check()
    assert any("volatile pointer cast" in e for e in errors)
    assert any("register/window address" in e for e in errors)
    build.build_id.cache_clear()
    original = build.build_id()
    (root / "app/._app.c").write_bytes(b"\x00\xff\xfe")
    build.build_id.cache_clear()
    assert build.build_id() == original, "metadata must not change the firmware build ID"
    build.WIFI_OWN = build.LINK = True
    flags = build.wifi_own_flags(host=True)
    includes = [Path(f[2:]) for f in flags if f.startswith("-I")]
    assert len(includes) == 3 and all(p.is_absolute() for p in includes)
print("PASS: AppleDouble metadata skipped in checks/build ID, MMIO violations rejected, host include paths absolute")
