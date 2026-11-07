# Installing and recovering

Audio Router targets the M-VAVE FM-1. The development device has
**m-bootloader already installed**. Installing this firmware must preserve it.

## Before the first install

Keep a verified full-flash backup and a working recovery route for your own
unit. A settings/preset export is not a firmware backup. The
[MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker) project provides
Linux backup/recovery tooling; read its instructions and the
[m-bootloader documentation](https://github.com/jvitkauskas/m-bootloader).
Verify the device identity, flash map and backup before any write. Never erase
the whole chip or write the boot region below `0x4000`.

## Updating with M-Flasher

1. Download the release's `audio-router.fwsc`, or use the same file in a
   successful Firmware Actions artifact. Locally it is always at
   `dist/audio-router.fwsc`.
2. Verify the accompanying `SHA256SUMS`. Use a reliable USB data cable and
   stable power.
3. Open [M-Flasher](https://github.com/jvitkauskas/m-flasher), select the FM-1
   and the firmware file. **Keep bootloader replacement disabled.**
4. Leave **“Also erase settings and Bluetooth pairings” unchecked** for ordinary
   Audio Router updates. It also erases the flash sectors containing FX presets.
5. Complete the update without interrupting USB/power; then verify audio and
   controls. Firmware-only writes preserve the settings, preset and bond areas.

The firmware container embeds the pinned **m-bootloader 2.2-64f7c9c for FM-1**
in its boot area. Release assets also include the identical plain
`fm1-uboot.boot` and its MIT notices. This replaces the SDK SPL in generated
containers. Firmware-only updates still preserve the bootloader already on the
device; bundling does not install it. The offline verifier checks the exact
bootloader bytes, ROM encoding, configuration and checksums, and that normal
firmware writes remain within `[0x4000, 0x93000)`.

## Recovery and useful distinctions

The platform retains the updater, watchdog/crash recovery and OCT- startup
rescue path. Follow the installed bootloader's README for its own entry/retry
procedure. A crash may intentionally suppress Bluetooth on the next boot:
open Settings and explicitly SAVE Bluetooth RECEIVER to retry after collecting
[logs](../DEBUG.md). This guard avoids an endless radio crash loop.

For interrupted updates, use the bootloader/updater recovery path. If the unit
enumerates as `WL82 UBOOT`, do not format it; follow the verified backup and
recovery tool's procedure for the firmware region. If USB no longer enumerates,
hardware recovery may be necessary. Do not copy raw write offsets or flash
identities from another firmware/device.

Host simulated recovery checks are part of packaging. Power-loss recovery and
analog/RF behavior still require testing on the physical unit.
