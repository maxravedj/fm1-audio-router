#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Current project regression suites. No device access. Build --host first."""
from pathlib import Path
import argparse
import subprocess
import sys

SRC = Path(__file__).resolve().parents[1]
ROOT = SRC.parent
APP = SRC / 'apps/fm1-audio-gateway'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--suite', choices=('ui', 'dsp', 'bluetooth', 'all'), default='all')
args = parser.parse_args()

def run(path, *extra):
    subprocess.run([sys.executable, str(path), *map(str, extra)], cwd=ROOT, check=True)

if args.suite in ('ui', 'all'):
    run(SRC / 'tests/build_version_test.py')
    run(SRC / 'tests/release_publish_test.py')
    run(SRC / 'tests/bundled_boot_test.py')
    run(APP / 'tests/build_checks.py')
    run(SRC / 'tests/screenshot_cli_test.py')
    run(SRC / 'tests/ui_profile_cli_test.py')
    run(APP / 'tests/run_monitor_codec.py',
        'mixer_signal_test', 'mixer_solo_test', 'usb_out_test', 'mixer_layout_test',
        'fx_gui_test', 'fx_integration_test', 'fx_presets_test', 'fx_preset_flash_test',
        'settings_scroll_test', 'settings_dirty_test', 'aptx_settings_test', 'gain_range_test', 'badge_layout_test', 'screenshot_test',
        'aac_display_recovery_test', 'display_memory_test', 'spectrum_memory_test',
        'spectrum_test', 'spectrum_controls_test', 'waveform_cost_test',
        'profile_ui_test', 'navigation_test', 'parameters_test')
if args.suite in ('dsp', 'all'):
    run(APP / 'tests/run_master_fx.py')
    run(APP / 'tests/run_bt_pcm.py')
    run(APP / 'tests/run_feedback.py')
    run(APP / 'tests/run_cpu2.py')
if args.suite in ('bluetooth', 'all'):
    run(SRC / 'tests/run_bt_ui_service.py')
    run(SRC / 'tests/run_bt_aptx.py')
    run(APP / 'tests/run_bt_page.py')
    run(APP / 'tests/run_profile_setup.py')
    run(SRC / 'tests/run_bt_sbc_cpu2.py', '--out', ROOT / 'build/tests/sbc')
    run(SRC / 'tests/run_bt_aac_transport.py', '--out', ROOT / 'build/tests/aac-transport')
    run(SRC / 'tests/run_bt_resample_cpu2.py', '--out', ROOT / 'build/tests/resample',
        '--fixtures', ROOT / 'build/tests/aac-transport', '--sbc', ROOT / 'build/tests/sbc')
print('PASS: Audio Router', args.suite, 'suite (host evidence; physical RF/listening not simulated)')
