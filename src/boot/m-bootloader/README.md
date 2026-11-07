# Bundled FM-1 bootloader

Unmodified `fm1-uboot.boot` from [m-bootloader v2.2](https://github.com/jvitkauskas/m-bootloader/releases/tag/v2.2),
commit `64f7c9cad1580bea870fc4a9e9bd61583fee3aef`, identity `fm1 / 2.2-64f7c9c`.
SHA-256: `cedc9c17648bdeecda5c8d0ffffe2211849f8a36ab615fe49d6cc46d249baf44`,
verified against the release SHA256SUMS and GitHub asset digest. MIT license
and upstream NOTICE retained beside the binary.

The app's `fm1.toml` pins this binary and hash. Packaging validates its BANKCB
header, payload checksums, load address and FM-1 board identity, then separately
ENC-encodes its header and payload into the firmware container's boot area.
The app and OTA loader are unchanged by this choice. Firmware-only installation
preserves the bootloader already on the device. The release also includes the
plain `.boot` file for tools that accept it; bundling never performs a write.

Read [upstream installation and recovery instructions](https://github.com/jvitkauskas/m-bootloader/tree/64f7c9cad1580bea870fc4a9e9bd61583fee3aef).
The old 1.2 installer under `src/tests/fixtures/` remains a separate OTA regression
fixture; it is not the bootloader embedded in new Audio Router packages.
