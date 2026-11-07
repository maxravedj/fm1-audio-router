# Building, testing and releasing

## Dependencies

Use Linux x86-64. The verified open pi32v2 toolchain is fetched automatically;
no vendor compiler or full SDK checkout is needed.

Arch Linux:

```sh
sudo pacman -S --needed base-devel git python python-pip rust ffmpeg curl
python -m venv .venv
. .venv/bin/activate
pip install -r requirements-build.txt
```

Ubuntu:

```sh
sudo apt-get install build-essential git python3 python3-venv python3-pip rustc ffmpeg curl
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-build.txt
```

Pillow generates the font headers. FFmpeg generates independent codec fixtures;
Rust builds the offline M-Flasher verifier. MIDI debugging dependencies are
separate: see [DEBUG.md](../DEBUG.md).

## Firmware

From the project root:

```sh
python src/scripts/setup_build.py
python src/scripts/build_gateway.py
python src/scripts/package_gateway.py
```

Setup verifies SHA-256 pins for the compiler archive and three Apache-licensed
SDK packaging files, and checks the exact toolchain-adapter commit. Downloads
and tools stay outside Git. The vendored BSP is included in the source tree;
there are no submodules to initialize. Pins are in `src/scripts/setup_build.py`
and [UPSTREAM.md](UPSTREAM.md).

The FM-1 m-bootloader binary is vendored under `src/boot/m-bootloader/` with
its license and provenance. The app configuration pins its SHA-256; build and
package checks reject modified, corrupt or wrong-board images. The generated
firmware container embeds it, and release assets include the plain `.boot`
file. Firmware-only installations preserve the device's existing bootloader.

- `build/gateway/`: ELF, disassembly, generated headers, loader and memory reports.
- `dist/audio-router.fwsc`: stable path for M-Flasher, refreshed atomically
  only after a successful target build; accompanied by `SHA256SUMS`.
- `dist/release/`: verified firmware, matching source archive, debug symbols,
  plain FM-1 bootloader, manifest, licenses and checksums, created by `package_gateway.py`.
- `logs/ci/`: linked-runtime, package parser and simulated OTA check results.

`JIELI_TOOLCHAIN` and `AC79_SDK` can override local dependency locations.
The wrapper enables optimized LLVM IR checks by default. The target build
rejects SRAM/stack/ROM/register violations; packaging additionally checks linked
IRQ ownership, scratch capacity, decrypted package identity and simulated
interrupted updates. No build script opens or flashes a device.

## Tests

```sh
python src/scripts/build_gateway.py --host
python src/scripts/test_gateway.py
```

Use `--suite ui`, `--suite dsp` or `--suite bluetooth` for focused runs.
These exercise actual app/platform code with sanitizers, independent codec
fixtures, threaded worker lifecycle tests, sample/response oracles, UI pixels,
persistence, routing and protocol/register models. They do not model physical
radio timing or prove analog output quality and device CPU headroom.

## Versions and GitHub Actions

There is no source version counter and no automatic tagging. Untagged builds
show `DEV-<commit>`. An exact Git tag supplies the release version, including the
splash/profile label and firmware identity. Use `[v]MAJOR.MINOR.PATCH`, optionally
with a prerelease such as `-rc.1`; each numeric component is 0..99 because the
FM-1 package identity encodes two digits per component. Reuse neither a tag nor
its identity for a different release.

When you choose a version, tag the reviewed master commit and push that tag:

```sh
git switch master
git tag -a v1.2.3 -m 'Audio Router 1.2.3'   # replace with your chosen version
git push origin master
git push origin v1.2.3
```

The example does not reserve a version. `build_gateway.py --tag v1.2.3` builds
that explicit label from a source archive without Git; it creates no tag.

`.github/workflows/firmware.yml` runs three test suites for master pushes, pull
requests and manual runs, then builds and verifies firmware. Download the
`audio-router-<commit>` Actions artifact for the firmware and matching sources.
A pushed tag additionally publishes those verified assets as a GitHub Release;
prerelease tags are marked accordingly. Only that final job has repository
write permission. No GitHub Pages configuration is used. Normal runs never
create a version or release.

Before distributing a hardware release, check playback, USB recording,
SBC/AAC pairing and reconnection, fades, CPU headroom and recovery on a real
unit; record what was verified in the release notes.
