# Audio Router project guidance

Read both local FM-1 skills before firmware/build/recovery work:
- `.agents/skills/mvave-fm1-firmware/SKILL.md`
- `.agents/skills/mvave-fm1-software-development/SKILL.md`

The actual fm1-base API in `src/include/plat.h`, the app, device code and
vendored BSP take precedence over generic Dinghy examples. Current design,
controls, build/release instructions and recovery guidance are in `docs/`.

## Layout and releases

- Keep firmware, platform, tests and build scripts under `src/`.
- Generated files belong in ignored `build/`, `dist/`, `logs/`, `tools/` and
  `downloads/`. Never commit flash dumps, toolchains, credentials or macOS metadata.
- `python3 src/scripts/build_gateway.py --host` builds the simulator;
  `python3 src/scripts/test_gateway.py` runs the current host suites.
- Firmware: `python3 src/scripts/setup_build.py`, then
  `python3 src/scripts/build_gateway.py` and `python3 src/scripts/package_gateway.py`.
  Each successful build refreshes `dist/audio-router.fwsc` atomically.
- The container bundles the pinned FM-1 m-bootloader in `src/boot/m-bootloader/`;
  validate its hash/BANKCB/board and ROM encoding. Bundling authorizes no device
  write; firmware-only updates must preserve the installed bootloader.
- Versions come only from manually supplied Git tags `[v]MAJOR.MINOR[.PATCH]`
  (optional prerelease; numeric components 0..99 for the FM-1 identity).
  Untagged builds show DEV plus the commit. Do not increment hardcoded versions
  or create numbered build/docs/release folders. Do not invent or push tags.
- GitHub Actions tests/builds pull requests and master; pushed tags additionally
  publish only `fm1-audio-router-vMAJOR.MINOR.PATCH.fwsc` as a release asset.
  Matching sources/debug symbols/notices stay in Actions artifacts; release
  descriptions retain notices; GitHub provides tagged source archives. No website.

## Runtime invariants

- USB playback and recording both remain enabled. Asynchronous feedback is on;
  USB console stays off because it conflicts with the feedback endpoint.
- Audio order: input trims/gates, USB+BT mix, EQ, limiter, one MASTER gain,
  premute MASTER telemetry, output mute. USB OUT routing is pre-MASTER and has
  its own trim/gate; source-only post-FX routes have independent DSP history.
- CPU0 owns UI, radio/signalling, allocation, USB and audio/mixing/FX. CPU1
  exclusively decodes SBC/AAC/aptX/aptX HD/LDAC and performs the exact 96-tap resampler. No
  single-core fallback. Stop dispatch and join before freeing/resetting workers.
- Keep all runtime code/constants in SRAM. Audio/USB/panel IRQs must continue
  during settings writes; never allocate, wait, draw or save in audio callbacks.
- Preserve the 8192-byte scratch guard and linked stack checks. SBC and aptX/HD plus
  spectrum plus maximum recording must fit. Release hidden graph producers
  before buffers; AAC suppresses both graph views before workspace allocation.
  LDAC allocates its decoder first, then admits waveform/spectrum only when they
  fit alongside maximum USB recording; unavailable graphs fall back to meters.
  Settings v17 extends the v16 advertised-codec mask with LDAC.
  Persistent SBC/aptX/USB route allocations use the high end to avoid fragmentation.
- aptX's 24-bit FIR uses the BSP's register-only stereo lane MAC kernel. Keep
  the high-word ring word-aligned, interpolation exact and each half's absolute
  coefficient sum <=50573; do not replace it with memory-MAC instructions.
  The decoder paired multiply consumes both wide products before any loads and
  must remain spill-free. Native arithmetic/ABI checks run during packaging;
  instruction counts do not establish real-time hardware headroom.
- The codec checklist has its own temporary mask: OCT- cancels, OCT+ OK updates the
  Settings draft, and screen navigation discards unconfirmed checkbox edits.
  Only the main Settings SAVE persists/applies settings. SEL is bright while
  the codec row is focused or its dialog is open. Both OCT buttons are fully
  lit inside the dialog; they cannot trigger Bluetooth pairing there.
  All five codecs are visible; SELECT moves the cursor, SEL toggles a checkbox.
  Settings shows red UNSAVED and full SAVE for a changed draft; unchanged Settings
  keeps SAVE dim. Unsaved current EQ/limiter presets also light SAVE fully.
- LCD and EQ/FFT foreground checkpoints feed already-allocated, encrypted media
  queues at most once/ms, eight ACL fragments at a time. Never replace this with
  a full radio poll during drawing: signalling can free display scratch or join
  workers. Preserve FIFO control deferral, workspace gates and main-thread ownership.
- Bypass delay changes retain the requested 5-ms down/silent switch/5-ms up;
  settled global/limiter bypass has zero delay. Prepare limiter detection during
  fade-out but apply ceiling guards to the active limited path. Preserve warm
  EQ state for identical paths. The limiter is sample peak, not true peak.
- Preserve saved-bond mutual authentication, encryption gates, teardown/session
  resets and manual-disconnect hold. Initial profile timeout retires after the
  first successful configuration on that ACL. Do not weaken security to fix
  interoperability. Retain the radio crash guard and explicit settings SAVE retry.
- Preserve settings/preset migration and flash map; preset slots save only by
  explicit confirmation and never include bypass state. See architecture docs.

## Safety and evidence

- Never flash/write a device without explicit approval and verified full backup
  plus recovery path. The user's m-bootloader is installed: never replace it,
  erase the boot sector or erase the whole chip. Build/release work authorizes
  no device I/O. Ordinary updates keep settings/Bluetooth erase unchecked.
- Preserve licensing, upstream pins and exact matching source distribution.
  Report host/model evidence separately from physical RF/listening evidence.
- Use focused tests for changes; CI runs all current project suites. Do not
  claim GitHub-hosted runs or hardware validation from local checks.
- Do not use GPT-6.1-Sol Medium subagents. Delegate only when explicitly requested.
