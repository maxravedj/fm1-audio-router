# Upstream and dependency pins

| Component | Source / revision | Notes |
| --- | --- | --- |
| Project skills | `maxfridbe/mvave-fm1`, `5607a600be7184f566dee46f0e53afb5595d0d48` | Local copies in `.agents/skills/`, MIT license retained. |
| fm1-base | `w0ts/fm1-base`, `a3b5da168bc66ff41fc02b413c48e84f5f118709` (full history retained in Git) | Platform fork; original docs in `docs/upstream/`. |
| felucca-bsp | `hugelton/felucca-bsp`, `8dcc0528ba7c561e96e4f593d3bb01b757dadfdf` | Vendored in `src/bsp/`; notices retained. |
| Open pi32v2 compiler | `jvitkauskas/pi32v2-llvm`, release `pi32v2-23.1.5-2` | Archive SHA-256 `cb896562418a2be93edb9bac26af4bc81a106a32974002a78e6b9fc203c93990`. |
| Compiler drop-in adapter | `jvitkauskas/pi32v2-isa`, `66ac37bb2ae40e5dbbd5588a6a7c2d7baf829d7f` | Downloaded into ignored `tools/`, checked by setup. |
| AC79 packaging files | `amitv87/fw-AC79_AIoT_SDK`, `ffbb2a8d925682ba6880d643a83a63ee0691ec09` | Only three files and license, pinned individually in setup; no vendor radio library linked. |
| m-bootloader | `jvitkauskas/m-bootloader`, v2.2 / `64f7c9cad1580bea870fc4a9e9bd61583fee3aef` | FM-1 binary in `src/boot/m-bootloader/`, SHA-256 `cedc9c17648bdeecda5c8d0ffffe2211849f8a36ab615fe49d6cc46d249baf44`; MIT license and NOTICE retained. |
| Offline M-Flasher parser | `jvitkauskas/m-flasher`, `55da95cc978e45d60d833a10edc3292c2a5918c2` | Unmodified `formats.rs` plus MIT license in `src/tests/vendor/m-flasher/`. |
| AAC-LC decoder | `charlesvestal/fm1-radio`, `c9113b5eceb9d6c295e32c86e5d769ed8efa67d6` | PacketVideo/AOSP fixed-point subset; local fixes/pins/notices in `src/bt/br/aac_dec/`. |
| SBC decoder | AOSP/Open Interface | Apache-2.0; provenance and marked adaptations in `src/bt/br/sbc_dec/README.md`. |

The toolchain's version is a dependency pin, not the firmware release version.
Firmware versions are chosen manually through Git tags. Setup verifies compiler
and SDK hashes and refuses mismatches. Do not replace them with unpinned latest
URLs to fix a download failure.

Font licenses are beside their assets in `src/assets/fonts/`. Platform, BSP,
Bluetooth, micro-ecc, PacketVideo and test-fixture licenses/notices remain in
the matching source archive. See [LICENSING.md](../LICENSING.md).
