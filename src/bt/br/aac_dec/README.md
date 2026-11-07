# AAC-LC decoder (AAC-1)

This is an independent, allocation-free raw AAC access-unit decoder, separate
from the transport. Audio Router 0.0.20 connects it through
`../bt_receiver_aac.c` for A2DP AAC-LC playback. Host proof does not
establish Bluetooth AAC or device playback.

Source: [charlesvestal/fm1-radio](https://github.com/charlesvestal/fm1-radio),
commit `c9113b5eceb9d6c295e32c86e5d769ed8efa67d6`,
`app/third_party/pvaac/`, examined 2026-11-05. Its upstream is PacketVideo's
Android 4.1.2 AAC decoder (`android-4.1.2_r1`, frameworks/av media/libstagefright/codecs/aacdec).
The local source-only reference clone is `src/reference/fm1-radio-aac`.
`UPSTREAM-SHA256SUMS` records the **original upstream** versions of retained files;
local patched files intentionally differ. All original notices remain in place.

Licensing: decoder and wrapper are Apache-2.0. Keep `pvaac/NOTICE`,
`pvaac/MODULE_LICENSE_APACHE2`, original file headers and `pvaac/README.fm1`
with source redistribution. `NOTICE` includes the Apache licence text and Android attribution; the
source headers retain PacketVideo attribution. No fm1-radio application/MPL wrapper code was copied.
Do not remove historical attribution inside the PacketVideo sources.

## Build and API

Compile `aac_dec.c` and exactly the 57 C files in `sources.txt`, with the module
and `pvaac/` include directories. Force-include `pvaac/pv_fm1.h`. Use C99,
`-fwrapv`, and the target's normal r3/FPU/erratum flags. Target uses `-Os`;
`-O2` is also exercised on host. Do not define `AAC_PLUS`, `HQ_SBR` or
`PARAMETRICSTEREO`; the forced header rejects these. No HE/SBR/PS/LTP
implementation source is retained. Some LTP-named state remains because the LC
filterbank uses that same storage for output/overlap. Headers for inactive
upstream declarations do not enable those codecs.

`aac_dec_workspace_size()` returns all persistent decoder storage. Supply an
8-byte-aligned workspace to `aac_dec_init`; no heap allocation occurs. Keep it
for the lifetime of the opaque handle. `aac_dec_reset` fully reinitializes the
state and replays the saved configuration. `aac_dec_destroy` clears the
workspace and invalidates the handle; it does not free the caller's memory.
Decoder calls require exclusive ownership and never run in an audio/USB IRQ.
On Audio Router's `cpu2` branch, the second core initializes, resets and decodes;
the first core joins outstanding work before destroying/freeing the instance.

ASC is deliberately strict: exactly two compact bytes, Audio Object Type 2
(AAC-LC), indexed 44.1 or 48 kHz, explicit mono or stereo, 1024-sample windows,
no dependency/extension flags/PCE. Explicit 24-bit sample-rate encodings and
extension-bearing ASCs are unsupported in AAC-1. MPEG-2 LC and MPEG-4 LC share
this decoder configuration; no Bluetooth codec signalling has been added.

Decode one complete raw `raw_data_block` per call, up to 2048 encoded bytes.
This byte bound exceeds the ordinary 1536-byte stereo AAC-LC maximum frame
budget and is not an advertised A2DP bitrate cap. ADTS/ADIF, RTP and LATM are
not accepted as framing by this API. PCM output requires capacity for 2048
`int16_t` samples even for mono. Always consume the returned frame count,
channel count, sample rate and bytes used. Output is interleaved signed 16-bit
PCM; no MASTER gain, routing or sample-rate conversion is applied here.

Workspace, encoded input, PCM output and metadata must not overlap. Decode
rejects their overlap. The init result-pointer storage must also be separate
from workspace/ASC; this is a caller precondition. After PacketVideo reports a
decode error, PCM is cleared, metadata has no frames and the decoder resets.
Zero/oversized AUs also reset the decoder, but leave the caller's output buffers
untouched. Discard PCM and metadata on every non-OK return; the next AU after
a data error starts with fresh filterbank history. Argument errors leave PCM untouched. The caller still
owns valid pointer/lifetime/sized-buffer preconditions, as with other C APIs.

## Changes from the pinned port

All decoder edits are marked `audio-router`; inherited `fm1-radio` bounds fixes
and the out-of-line `fm1_fxp.c` 64-bit multiply workaround are preserved.

* Remove LTP parsing and synthesis; remove ADIF/ADTS sniffing; reject in-band PCE.
* Report configured channels and object type in Config (upstream reported two
  channels unconditionally and did not populate the object-type field there).
* Give inline bit readers translation-unit linkage so sanitizers can stop
  inlining them without introducing unresolved external calls.
* Validate actual quantized spectral coefficient magnitudes before inverse
  quantization. Mutated escape/pulse syntax could leave the running maximum
  too small and select a direct inverseQuantTable lookup out of bounds.
* Remove unused final short-window speculative prefetches. These read one
  sample beyond/before window tables; upstream fuzz excluded the table object
  from ASan. This build instruments the tables normally.
* Define transform exponent shifts for extreme scalefactors: arithmetic right
  shifts saturate reverse shifts and sign-extend excessive right shifts.
  Normal representable shifts retain their values.

## Host evidence

From the project root run `python3 src/tests/run_bt_aac.py` (requires a C compiler
with ASan/UBSan and ffmpeg). Generated fixtures, extracted raw AUs, independently
decoded reference PCM, actual PCM and metrics go under
`build/0.0.19/aac-host/`; commands and sanitizer output go to
`logs/0.0.19/aac-host.log`. Fixture signals use 997 Hz left/mono and 2309 Hz right
at different amplitudes; matching each channel independently detects swaps,
downmixing, polarity/level errors and sample count differences. Generated PCM
is available for listening; no physical listening/device test is claimed.

The matrix covers 44.1/48 kHz x mono/stereo x 128/192/256 kbps/VBR (16 fixtures,
200 raw AUs), each with exact PCM sample-count equality and per-channel
correlation >0.999, SNR >35 dB, level delta <0.1 dB relative to ffmpeg. Tests also
exercise all 65,536 compact ASCs, null/overlapping/unaligned inputs, undersized
workspace/output, zero/oversized AUs, truncated prefixes and seeded random and
mutated AUs, plus reset/recovery determinism. Sanitized runs are time bounded.

Sanitizer arithmetic contract is explicit: `-fwrapv` preserves PacketVideo's
inherited two's-complement signed fixed-point overflow assumptions;
`-fno-sanitize=shift-base` excludes signed negative/overflowing **left operands**
of shifts. Shift-exponent checks, bounds, alignment, object checks and all ASan
checks remain enabled. There is no window-table or other OOB exclusion. The fixed seed is `0x194aac`; the matrix runs 5,000 random/mutation decodes
(25 per valid AU) plus truncated prefixes. These
are bounded regression/fuzz tests, not a proof for every malicious bitstream.

## Memory and target limitations

64-bit host workspace: 48,928 bytes including wrapper and decoder state. PCM
needs another 4096 bytes; encoded input is caller-owned (at most 2048 bytes).
Measured worst channel metrics: correlation 0.9999999753, SNR 72.96 dB,
absolute level delta 0.000638 dB.

The standalone target probe `python3 src/scripts/probe_bt_aac_target.py` reports
actual pi32v2 code/rodata, 32-bit workspace and static stack estimate under
`build/0.0.19/aac-target/`; its measurements are separate from hardware evidence.
Final standalone pi32v2 `-Os` probe: text 22,968 bytes; rodata 22,908 bytes;
workspace 48,768 bytes (88 wrapper + 48,680 PacketVideo); no writable globals.
Static optimized-IR stack analysis reports 588 bytes from `aac_dec_decode`
(624 including synthetic entry), with no unresolved indirect-call notes. This
is a static estimate, not measured hardware high-water usage. The target
erratum and ROM-constant scanners report no hazards and need no exception.

The final live path still needs a budget for LATM assembly, PCM queues/rate
matching, UI buffers and runtime stack. No flash layout, bootloader or device
writes are part of this module.
