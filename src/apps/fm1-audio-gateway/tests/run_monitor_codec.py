#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Focused codec/telemetry UI integration, ASan + UBSan; no device access."""
from pathlib import Path
import os, subprocess, sys
app=Path(__file__).resolve().parents[1];src=app.parents[1]
out=(app/'build').resolve()/'monitor-tests';out.mkdir(exist_ok=True)
if not sys.argv[1:]:
    exe=out/'codec_preference_test'
    subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O1','-g','-ffp-contract=off','-fsanitize=address,undefined,float-cast-overflow',
        '-fno-sanitize-recover=all',str(app/'tests/codec_preference_test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
for name in (sys.argv[1:] or ('mixer_signal_test','mixer_solo_test','usb_out_test','mixer_test','mixer_layout_test','ui28_test','ui27_test','fx_gui_test','fx_integration_test','wave_settings_test','monitor_codec_test','monitor_layout_test','audio_contract','bt_settings_test','navigation_test','display_memory_test','display_test','parameters_test','spectrum_controls_test')):
    exe=out/name
    subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O1','-g','-w',
        '-ffp-contract=off','-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize-recover=all','-DPLAT_HOST','-DFM1B_CPU2=1','-DFM1B_UAC=1','-DFM1B_UAC_OUT=1',
        *[f'-I{p}' for p in (src/'device',src/'include',src/'lib',src/'bt/br/sbc_dec',app,app/'build/gen')],
        str(app/f'tests/{name}.c'),*map(str,sorted((src/'bt/br/sbc_dec').glob('*.c'))),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: focused monitor/codec UI suite, real telemetry producer and PCM mixer; RF is not simulated')
