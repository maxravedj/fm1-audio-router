# LDAC receiver

LDAC is available in the Bluetooth codec checklist. It uses
Sony's fixed-point decoder from pinned
[open-vela/external_libldac](https://github.com/open-vela/external_libldac/tree/6e0474c954103ff7aefb7908b97e65522c9208fa)
on CPU1, with the existing exact 96-tap 24-bit resampler. Stereo 44.1 and 48 kHz
are advertised; mono, dual-channel and higher sample rates are rejected.
The source chooses the codec and quality. Existing defaults remain SBC + AAC.

Open Settings → Bluetooth codecs with SEL, check LDAC using SELECT/SEL,
confirm the checklist with OCT+, then SAVE Settings to persist and apply it.
The connected badge shows `BT LDAC`. See [controls](CONTROLS.md) and
[debug commands](../DEBUG.md).

## Transport and worker

CPU0 validates RTP v2 headers, CSRC/extensions/padding, LDAC frame counts and
exact frame boundaries before publishing complete immutable frames. Fragmented
frames have bounded storage and require matching sequence, timestamp and
remaining-fragment count. Duplicates/reordered packets are discarded; loss and
sender changes schedule an overlap-history reset at the next accepted packet.
Queue pressure drops complete packets and requests a reset as well. Timestamp
steps between complete, ordered packets are diagnostic only: some Android
sources send intact codec frames with one-frame timestamp offsets. Resetting
on those offsets repeatedly destroys transform overlap and distorts audio.
Decoded frame counts drive PCM timing; timestamps still must agree across all
fragments of a single codec frame.

CPU1 decodes at most two 128-sample frames per job and runs the shared 24-bit
FIR. A failed frame produces timed silence, never suspect PCM. Decoder state,
compressed FIFO, fragment assembly, low-byte PCM ring and output workspace are
allocated together before dispatch, when AVDTP enters a configured/open/playing
state. Pause also reserves this workspace after its joined reset, so graph
availability does not depend on a media packet arriving. CLOSE/teardown releases
the reservation. Decode/reset have no heap calls. IRQs only
reap/dispatch prepared work; they never allocate, decode or wait. Teardown stops
dispatch and joins before freeing storage. Security and reconnect behavior use
the existing encrypted-session gates and manual-disconnect hold.

Before consuming live packets, CPU1 decodes two known reference frames using
the existing decoder and output scratch. The first primes overlap; the second
has independently verified left/right hashes. The probe resets history and
never publishes audio. `LDAC check` reports mask 3 when both channels match;
failure suppresses audio for the current allocation. This checks actual native
decoding without relying only on host tests. It needs no additional allocation.
Wide multiply helpers consume rounding/shifting before returning, so callers
cannot insert loads between multiplication and consumption of the product.

Protocol references:

- [AOSP LDAC constants](https://android.googlesource.com/platform/packages/modules/Bluetooth/+/cdb1c6dca4/system/stack/include/a2dp_vendor_ldac_constants.h):
  Sony vendor `0x0000012d`, codec `0x00aa`, rate/channel bits and media flags.
- [AOSP LDAC packet construction](https://android.googlesource.com/platform/system/bt/+/78003bf5ef7e3d5ff8a3c4e8452bd5fa05e7e4ee/stack/a2dp/a2dp_vendor_ldac.cc):
  the media count denotes frames in a complete packet, or remaining fragments
  including the current fragment in a fragmented frame.

## SRAM and graph fallback

All runtime instructions and constants remain in SRAM. LDAC first releases
waveform/spectrum producers and their buffers, then allocates decoding storage.
Graph allocation is attempted only after that storage exists, retaining room
for maximum USB recording and the 8,192-byte scratch guard. Spectrum yields
memory first; waveform is also replaced by full meters if it cannot fit.
Allocation retries when space becomes available, and disconnect restores a fresh
requested graph rather than stale capture. AAC keeps its existing meter fallback.
HOME cycles only views that actually allocate. An unavailable spectrum page is
skipped, so LDAC cycles meters → waveforms → meters while enough SRAM remains
for waveform capture. A graph that already fell back to meters cycles from that
visible page, rather than inserting an additional meter page.

Linking another decoder also consumes SRAM while SBC/aptX are selected. To keep
their spectrum + maximum USB recording configurations valid, the FFT now expands
PCM backwards into its own complex workspace and reflects quarter-wave twiddles.
That saves 24,568 bytes of display storage without changing the stereo analysis.
Unused 88.2/96-kHz LDAC inverse-transform tables are discarded as well.

Measured linked budget with the pinned `pi32v2-23.1.5-2` toolchain
(always consult the current build's JSON after source changes):

| Item | Bytes |
| --- | ---: |
| Free linked SRAM, before dynamic scratch/guard | 107,936 |
| Scratch guard | 8,192 |
| Complete LDAC dynamic allocation | 23,776 |
| Of which decoder instance | 13,864 |
| Maximum USB recording allocation | 6,896 |
| Waveform display + capture | 61,000 |
| Spectrum display + capture | 73,736 |
| Remaining: LDAC + recording + waveform + guard | 8,072 |
| Remaining: aptX/HD + recording + spectrum + guard | 6,352 |

LDAC + spectrum + maximum recording would miss the guarded budget by 4,664
bytes, so spectrum currently uses meters during LDAC. These are allocation and
static stack checks, not a CPU utilization estimate. Build reports are
`build/gateway/cpu2-budget.json` and `sizes.json`; packaging repeats the checks.

The isolated decoder probe reports 7,026 bytes of text (including tiny probe
libc stand-ins), 9,656 bytes of read-only tables/probe vectors, 32 bytes of mutable
statics, 2,332 bytes of static decode call depth and 2,368 bytes for the startup
check. These isolated numbers are not added
on top of the linked budget: that budget already includes the decoder.

## Validation and remaining device checks

```sh
python3 src/tests/run_bt_ldac.py
python3 src/tests/run_ldac_target.py
python3 src/scripts/test_gateway.py --suite bluetooth
python3 src/scripts/test_gateway.py --suite ui
python3 src/scripts/build_gateway.py
python3 src/scripts/package_gateway.py
```

The host oracle reconstructs and hashes the pinned unmodified source before
building its encoder/decoder. At both rates it compares all 24 PCM bits for
HQ/SQ/MQ and continuous quality changes, including 24-bit stereo tones from
-40 to -130 dBFS. ASan/UBSan
checks cover 320,000 deterministic malformed/mutated frames plus truncation,
headers, reset, alias and capacity cases. The real transport/CPU1/FIR harness
also checks bundling/fragments/extensions/padding, sequence/timestamp/cursor
wrap, loss, bit-exact continuity across timestamp-only steps with varying packet
frame counts, caller-buffer reuse, saturation, allocation failure and joined
teardown. The prepared FIR output is consumed by the production mixer and device
BT peak tap: quiet nonzero PCM must reach numeric peaks even with empty bars.
UI tests cover all 31 advertised masks, configuration/reconfiguration,
v16/v17 settings, five visible checkboxes, draft-only confirmation, graph
fallback and recovery, and peak hold/decay across LDAC workspace transitions.
LCD transfer counts verify that unchanged meter pages keep their body intact
through start, pause and delayed CLOSE, including a continuing spectrum fallback.
FFT output is compared with an analytic Hann reference.

Host/model and native compile/link checks pass. Device logs confirm Android
negotiation and 48-kHz stereo decoding; the user reports working LDAC audio after
the fused native arithmetic change. Paused graph access and HOME skipping still
need device validation. Host tests and static linking do not establish sustained
headroom or interoperability across sources/quality modes. On hardware,
check 44.1/48 kHz and all source quality modes, sustained playback, screen
changes, FX, maximum USB recording, out-of-range reconnect and memory recovery.
Capture `log` while playing to retain `BT LDAC us`, FIR and queue diagnostics.
The additional `LDAC loss/jump/buf/hist/check` rows distinguish sequence loss,
FIFO drops, timestamp mismatches, actual overlap-history resets and known-input
native decoding; see [DEBUG](../DEBUG.md).

The [decoder README](../src/bt/br/ldac_dec/README.md) records API/provenance.
Upstream's license and separate certification NOTICE are preserved in source,
packaging and release notices; this port makes no certification claim.
