#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""M1 host verification; no device access. Run from any directory."""
from pathlib import Path
import os
import subprocess
import sys

APP = Path(__file__).resolve().parents[1]
SRC = APP.parents[1]
OUT = (APP / "build").resolve()


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


run(sys.executable, APP / "tests/build_checks.py")
run(sys.executable, SRC / "tools/build.py", APP, "--host")
host = OUT / "host/fm1audio_host"
run(host, APP / "tests/basic.txt", OUT / "host")
run(sys.executable, SRC / "tests/host_usb_audio_test.py", host, OUT / "usb-audio", "--db-master")
test = OUT / "host/audio_contract"
run(os.environ.get("CC", "cc"), "-std=c99", "-O2", "-ffp-contract=off", "-w",
    "-DPLAT_HOST", "-DFM1B_CPU2=1", "-DFM1B_UAC=1", "-DFM1B_UAC_OUT=1",
    *[f"-I{p}" for p in (SRC / "device", SRC / "include", SRC / "lib", APP, OUT / "gen")],
    APP / "tests/audio_contract.c", *sorted((SRC / "bt/br/sbc_dec").glob("*.c")), f"-I{SRC / 'bt/br/sbc_dec'}", "-lm", "-o", test)
run(test)
display = OUT / "host/display_test"
run(os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g", "-w", "-fsanitize=address,undefined",
    "-DPLAT_HOST", "-DFM1B_CPU2=1", "-DFM1B_UAC=1", "-DFM1B_UAC_OUT=1",
    *[f"-I{p}" for p in (SRC / "device", SRC / "include", SRC / "lib", APP, OUT / "gen")],
    APP / "tests/display_test.c", *sorted((SRC / "bt/br/sbc_dec").glob("*.c")), f"-I{SRC / 'bt/br/sbc_dec'}", "-lm", "-o", display)
run(display)
parameters = OUT / "host/parameters_test"
run(os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g", "-w", "-fsanitize=address,undefined",
    "-DPLAT_HOST", "-DFM1B_CPU2=1", "-DFM1B_UAC=1", "-DFM1B_UAC_OUT=1",
    *[f"-I{p}" for p in (SRC / "device", SRC / "include", SRC / "lib", APP, OUT / "gen")],
    APP / "tests/parameters_test.c", *sorted((SRC / "bt/br/sbc_dec").glob("*.c")), f"-I{SRC / 'bt/br/sbc_dec'}", "-lm", "-o", parameters)
run(parameters)
navigation = OUT / "host/navigation_test"
run(os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g", "-w", "-fsanitize=address,undefined",
    "-DPLAT_HOST", "-DFM1B_CPU2=1", "-DFM1B_UAC=1", "-DFM1B_UAC_OUT=1",
    *[f"-I{p}" for p in (SRC / "device", SRC / "include", SRC / "lib", APP, OUT / "gen")],
    APP / "tests/navigation_test.c", *sorted((SRC / "bt/br/sbc_dec").glob("*.c")), f"-I{SRC / 'bt/br/sbc_dec'}", "-lm", "-o", navigation)
run(navigation)
for name in ("spectrum_test", "spectrum_display_test", "spectrum_controls_test"):
    spectrum = OUT / "host" / name
    run(os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g", "-w", "-fsanitize=address,undefined",
        "-DPLAT_HOST", "-DFM1B_CPU2=1", "-DFM1B_UAC=1", "-DFM1B_UAC_OUT=1",
        *[f"-I{p}" for p in (SRC / "device", SRC / "include", SRC / "lib", APP, OUT / "gen")],
        APP / "tests" / f"{name}.c", *sorted((SRC / "bt/br/sbc_dec").glob("*.c")), f"-I{SRC / 'bt/br/sbc_dec'}", "-lm", "-o", spectrum)
    run(spectrum)

cpu = OUT / "host/cpu_workload_test"
run(os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-fsanitize=address,undefined",
    f"-I{SRC / 'include'}", f"-I{SRC / 'device'}",
    APP / "tests/cpu_workload_test.c", "-o", cpu)
run(cpu)
mix_test = OUT / "host/master_mix_test"
run(os.environ.get("CC", "cc"), "-std=c11", "-O1", "-w", f"-I{SRC / 'include'}",
    f"-I{SRC / 'device'}", f"-I{SRC / 'bsp/chip'}", f"-I{SRC / 'bsp/boards/fm1'}",
    APP / "tests/master_mix_test.c", *sorted((SRC / "bt/br/sbc_dec").glob("*.c")), f"-I{SRC / 'bt/br/sbc_dec'}", "-lm", "-o", mix_test)
run(mix_test)
controls = OUT / "host/master_controls_test"
run(os.environ.get("CC", "cc"), "-std=c11", "-O1", "-w", f"-I{SRC / 'include'}",
    f"-I{SRC / 'device'}", f"-I{SRC / 'bsp/chip'}", f"-I{SRC / 'bsp/boards/fm1'}",
    APP / "tests/master_controls_test.c", *sorted((SRC / "bt/br/sbc_dec").glob("*.c")), f"-I{SRC / 'bt/br/sbc_dec'}", "-lm", "-o", controls)
run(controls)
wave_test = OUT / "host/waveform_test"
run(os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-fsanitize=address,undefined",
    f"-I{SRC / 'include'}", APP / "tests/waveform_test.c", "-o", wave_test)
run(wave_test)
for mode in (0, 1):
    packet_test = OUT / f"host/packet_meter_{mode}"
    run(os.environ.get("CC", "cc"), "-std=c11", "-O1", "-w", f"-DTEST_O24={mode}",
        f"-I{SRC / 'include'}", f"-I{SRC / 'bsp/chip'}", APP / "tests/packet_meter_test.c",
        *sorted((SRC / "bt/br/sbc_dec").glob("*.c")), f"-I{SRC / 'bt/br/sbc_dec'}", "-lm", "-o", packet_test)
    run(packet_test)
print("PASS: M1 application contracts and host USB integration")
