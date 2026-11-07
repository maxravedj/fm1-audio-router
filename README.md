# FM1 Audio Router

Alternative firmware for the **M-VAVE FM-1**: mix USB and Bluetooth audio,
process the master output, and monitor signals on the built-in display.

- Stereo USB audio playback and recording (`FM1 Audio`).
- Bluetooth A2DP receiver with SBC and AAC-LC (`FM1 BT Receiver`).
- USB/BT input trims, USB output routing, master control, mute and solo.
- Five-band parametric EQ and lookahead peak limiter, with eight presets each.
- Stereo meters, scrolling waveforms, 40-band spectrum and dual-core CPU display.

Download `audio-router.fwsc` from **GitHub Releases** or a successful
**Actions → Firmware** artifact. Install with M-Flasher; keep bootloader
replacement disabled and **settings/Bluetooth erase unchecked** to retain
settings, presets and pairings. Read [installation and recovery](docs/INSTALL.md)
before the first install. No website or browser installer is provided.

Build on Linux x86-64:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-build.txt
python src/scripts/setup_build.py
python src/scripts/build_gateway.py
```

Firmware is always written to `dist/folder/audio-router.fwsc`. Versions come
from manually created Git tags; untagged builds are marked `DEV`.

[Controls](docs/CONTROLS.md) · [Build, tests and releases](docs/BUILD.md) ·
[Architecture](docs/ARCHITECTURE.md) · [Debugging](DEBUG.md)

Built on fm1-base and felucca-bsp. See [credits](CREDITS.md) and
[licensing](LICENSING.md). Host tests cover DSP, protocols and UI behavior;
hardware RF interoperability, sound quality and CPU headroom need device checks.
