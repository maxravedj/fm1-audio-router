# Runtime and audio architecture

The application is `src/apps/fm1-audio-gateway/`, built on the modified fm1-base
platform and vendored felucca-bsp. Its authoritative API is `src/include/plat.h`.
Generic Dinghy examples do not describe every detail of this runtime.

## Signal paths

```text
USB playback ─ trim/gate ─┐
                         ├─ sum ─ 5-band EQ/post ─ dynamics ─ MASTER ─ mute ─ codec
BT decode/FIR ─ trim/gate ┘
```

USB is UAC1 stereo 44.1-kHz/16-bit, with asynchronous playback feedback to
follow the actual ALNK clock. Recording stays enabled because the platform's
playback configuration depends on it. USB console is disabled to preserve the
feedback endpoint. Internal audio retains 24-bit/headroom precision through
mixing and FX. MASTER is applied exactly once.

Raw USB/BT monitor taps precede trims. MASTER monitoring follows FX and the
physical gain, preceding output mute/fade. Mixer meters follow trim but precede
their mute/solo gates so suppressed sources remain visible. Hidden producers
stop; waveform/FFT/vectorscope scratch is detached from IRQs before being freed.

USB OUT routes are OFF, Loopback, Loopback post FX, BT, BT post FX, Master,
and Master post FX. Loopback means USB alone; Master means USB+BT. Raw routes
ignore trims. Post-FX routes include relevant input trims/gates and current FX,
preceding physical MASTER and PLAY mute. USB-only/BT-only post routes have
independent DSP state; shared Master post FX reuses the master chain tap.
K3's OUT trim/gate applies before the final USB send/16-bit conversion. Monitor
telemetry follows it. Source-only DSP sleeps when the host is not recording;
OUT graphs show silence then, rather than starting that extra FX instance.

## Cores and real-time ownership

- CPU0: radio/signalling/RTP assembly, UI, USB, allocation, mixing, FX, panel
  and audio IRQs. Foreground computes EQ coefficients; audio publishes the
  pending bank under a short lock and alone mutates its DSP histories.
- CPU1: bounded compressed SBC/AAC/aptX/aptX HD/LDAC decoding and the exact 96-tap resampler.
  CPU0 consumes prepared int32 stereo output. A 2-kHz hook and audio IRQ can
  reap completed work/kick the mailbox without decoding, FIR work, waits or
  allocation on CPU0 IRQs. There is no single-core fallback.

Teardown disables dispatch and joins before reset/free. Immutable compressed
queue elements and memory barriers protect in-flight work and PCM publication.
Foreground retains fault/timeout/allocation ownership. A warm reset stops the
surviving worker before copying executable SRAM.

LCD operations and long EQ/FFT calculations include foreground media checkpoints.
At most once per millisecond they drain up to eight queued ACL fragments for the
active media channel, feeding the CPU1 decoder during a page repaint. They run
only with an encrypted streaming session and existing decoder/workspace storage.
Control traffic stays ordered at the head of the RX ring for the normal main-loop
poll: checkpoints never process signalling, change codecs, free display buffers,
allocate or wait for a worker. They do not run inside IRQs or replace normal radio
polling. Modelled continuous ingress/repaints establish this service behavior,
not measured RF or CPU timing on the FM-1.

All runtime code/constants reside in SRAM, allowing panel/audio/USB IRQs to
continue during settings flash writes. Main/IRQ/worker stacks and indirect
call sets are checked in the linked image. An 8192-byte scratch guard is kept.
SBC or aptX/aptX HD + spectrum + maximum USB recording must fit together. Exact linked budgets
are in `build/gateway/cpu2-budget.json` and release manifests; they change with
the chosen tag/toolchain, so they are not hardcoded in this guide.

Waveform auto scale applies to the selected USB IN, BT IN, USB OUT or MASTER
source. Both channels share the same bounded display gain, with immediate
expansion for larger peaks, slow recovery and a dead band for small changes.
It changes neither audio nor meter levels. The existing saved enable bit is
retained, so earlier MASTER-only preferences now apply to every waveform source.

Spectrum reuses its int16 PCM staging buffer for the complex float FFT,
expanding backwards after reading the DC mean. Quarter-wave twiddles are
reflected from one cosine table for the remaining phases. Capture plus FFT scratch is 69,640 bytes;
stereo power analysis, FFT resolution and Hann normalization are
unchanged. Each FFT stage visits phases before independent butterfly groups,
reusing each reconstructed cosine/sine pair (4,095 pairs per transform instead
of 24,576). Media service checkpoints remain bounded to 128 butterflies, with
no extra table allocation; the window stays at 4,096 samples and refresh at 33 ms.

Waveform history keeps six levels (1 to 1024 samples per bucket). The unused
4096-sample level is removed: every displayed window, including 8 seconds at
the current graph width, uses a finer level. Waveform capture and UI occupy
52,808 bytes, 8192 fewer than before.

Vectorscope shares the raw stereo capture producer with spectrum, but does no
FFT. It plots `x ∝ R−L`, `y ∝ −(L+R)` using integer arithmetic
from the latest 2048 paired samples (~46 ms), with a 16 ms refresh interval
(roughly 60 fps when workload permits). Late frames are not caught up in bursts. Two 1-bit
176×176 maps track current/drawn points; only changed 16-row canvas strips
reach the LCD. Only those 2048 pairs are copied, scanned for auto gain and plotted;
the spectrum reader retains its full 4096-frame window. Capture, staging, maps
and timing occupy 32,344 bytes, all
allocated on entry and detached/freed on exit. Hidden views do no capture or
plotting; screenshot and MIDI overlay replay paints the frozen map only.
Optional stereo-linked auto scale measures the snapshot peak with integer math;
it expands immediately for louder transients and gently enlarges quiet traces,
with a ~60 dB gain cap. One reciprocal per snapshot supplies two multiplies per
point. The gain changes neither audio nor meter levels.

AAC workspace takes priority over waveform/spectrum/vectorscope buffers. Accepted AAC
configuration synchronously releases graphs before allocation. They return
fresh after disconnect; failed visible graph allocation retries without
clearing the continuing meter/footer. SBC decoder context, synthesis history,
decoded frame and fragment staging are inside its dynamically allocated session workspace (7,768 bytes on target,
including the packet queue), released after joining CPU1. No SBC decoder scratch
remains reserved while another codec runs. Persistent SBC/aptX/USB route allocations
use the high end of scratch to preserve a contiguous graph gap. LDAC releases
graphs synchronously on configuration/state changes, allocates its complete
compressed FIFO/decoder/24-bit PCM storage first, including while configured or
paused, and only then admits graphs that fit alongside maximum USB recording.
AVDTP transitions join/reset and reserve the new workspace before accepting more
media, so paused graph access does not depend on the next audio packet.
Decoder workspace refreshes preserve the meter hold/decay state and its clock,
including numeric peaks below the graphical floor. If a graph temporarily falls
back to the four-meter layout, its selected-source stereo pair is remapped
without borrowing another source's peak. Reallocation on the same visible layout
does not clear the panel. Nongraph screens and a continuing full-meter fallback
keep their existing render caches and telemetry without a body repaint; codec
or connection changes update only the status bar. Screen/source navigation still
starts fresh meters.
Unavailable graphs use full meters; HOME skips them when cycling pages;
allocation retries and disconnect restore the requested view without stale
capture. The current linked budget admits LDAC plus waveform or vectorscope and recording;
spectrum yields memory. See [LDAC](LDAC.md) for measured sizes and validation.

## FX and timing

EQ uses cascaded trapezoidal SVFs with a stable-bank transition for edits.
Unchanged EQ paths copy their warm histories on unrelated limiter/post-gain
edits. Five bands offer bell/pass/shelf types; pass slopes cascade 1–4 stages.
Dynamics links stereo sample peaks and uses a monotonic lookahead deque and
final integer ceiling guards. Peak Limiter retains its finite curved
anticipatory gain ramp and manual exponential release. LM-Maximizer has an
inverse-gain anticipatory ramp and automatic fast/medium/long recovery driven
by input history. Its original, causal controller approximates the black-box
measurements described in [Dynamics](DYNAMICS.md). The two controllers share
an exclusive storage union; deque timestamps use bounded byte ages. Neither
mode allocates in the audio callback or changes CPU1 ownership.

Delay is 128 frames for Peak Limiter or 55 for LM-Maximizer, only while dynamics
output is active. Global/dynamics bypass and mode changes switch at silence
between 221-frame down/up envelopes. Detection may
warm during fade-out, but a pending ceiling must not clamp the old bypass
path. The active limited output is constrained through fade-in and steady
operation. A changing time alignment/level dip is not universally inaudible.

## Persistence and recovery

CRC-checked A/B objects retain panel calibration, application settings, saved
FX presets and bond metadata. Settings payload schema is 23 (32 bytes); FX
preset schema is 5 (522 bytes). These are storage format numbers, independent
of manually chosen firmware tags. Keep their migrations, not DSP history.
AFX v5 preserves the 456-byte v3/v4 prefix and appends eight LM-Maximizer slots
and its selected position. Peak Limiter and LM-Maximizer have separate working
banks, selected positions and explicit preset saves, with one shared Dynamics
bypass flag. Migration copies the old shared presets and selection into both
banks without a boot-time write. The larger record uses three payload pages
plus the final commit header in the existing FX A/B sectors.

LM-Maximizer exposes threshold as `ceiling − input gain` while retaining the
AFX v5 gain encoding. Ceiling edits adjust that gain by the same dB delta,
preserving the limiting threshold. Legacy LM thresholds outside −30..0 dB
are clamped only in the working bank; explicit preset SAVE owns persistence.
Peak Limiter's input-gain controls and saved values are unchanged.

Relevant physical flash offsets:

| Range | Owner |
| --- | --- |
| `0x00000..0x03FFF` | Installed bootloader; never replace/erase. |
| `0x04000..0x92FFF` | Ordinary firmware update region. |
| `0xD8000..0xD9FFF` | FX A/B objects. |
| `0xDA000..0xDBFFF` | Bluetooth address/bonds. |
| `0xE0000..0xE4FFF` | Updater staging/platform diagnostic allocation. |
| `0xFC000..0xFEFFF` | Platform globals/settings allocation. |

Other platform-owned data remains reserved. Verify the actual BSP object map
before changing any flash allocation. M-Flasher's optional data wipe includes
FX and bonds; firmware-only updates do not.

Bluetooth requires completed mutual authentication and encryption for AVDTP
and media; basic SDP/L2CAP control may precede encryption. Successful profile
configuration retires the initial setup timeout for that ACL, so stream
teardown cannot preempt its later manual-disconnect reason. Keep saved-key
and crossed-authentication handling. The radio crash guard intentionally skips
automatic radio startup after a crash; an explicit Settings SAVE retries it.

The receiver has two independent ACL/security/L2CAP/AVDTP sessions and one
shared decoder/FIR/PCM pipeline. The last accepted AVDTP START takes ownership;
the previous source receives AVRCP Pause press/release, and its late media is
excluded. CPU1 is joined before decoder ownership changes. Manual disconnect
holds follow peer addresses across slots; range loss remains eligible for
reconnection. Bounded control replies retry without consuming subsequent RX
or replaying already committed signalling commands. A real PCM starvation
uses a 64-frame down-ramp and a 64-frame up-ramp after the existing rebuffer
watermark; uninterrupted samples are unchanged. See [Bluetooth](BLUETOOTH.md)
for radio scheduling provenance, host evidence and required physical checks.

Host tests provide protocol/register, pixel, sample and lifecycle evidence.
Actual RF timing, power-loss recovery, CPU headroom and listening remain
separate device-validation tasks.

aptX and aptX HD use the pinned LGPL libfreeaptx decoder on CPU1. An optional
low-byte PCM ring keeps all 24 decoded bits through the rate matcher and pre-FX
mix; SBC/AAC keep their original 16-bit arithmetic. See
[src/bt/br/aptx_dec/README.md](../src/bt/br/aptx_dec/README.md) for transport,
source/relinking instructions and validation limits. Settings v16 stores the
codec checkbox mask in the existing codec byte, retaining the 24-byte record
and all v1-v15 migrations. Settings v17 adds LDAC in bit 4 without changing
the record size or previous bits. The advertised list applies only on successful SAVE.

Settings v18 keeps the 24-byte record: reserved bits 5/6 store MIDI controller
enable/C highlighting, and the high nibble of `bt_reserved[2]` stores channel
0–15 (displayed as 1–16). Older versions default MIDI OFF, highlight OFF and
channel 1. Codec masks, colors, USB routing, FX storage and flash sectors are
unchanged. Settings v19 appends seven last-exit encoder positions and a zero
reserved byte, extending the record to 32 bytes while retaining the v18 prefix
and all earlier migrations in the same A/B sectors. Old records initialize
positions to 64. Settings v20 reuses the final reserved byte for vectorscope
color and adds Home mode 4, keeping the 32-byte size and all flash sectors.
Versions through v19 default vectorscope to green; v19 still validates its
reserved byte as zero. Settings v21 uses bit 3 of the final byte for vectorscope
auto gain (default ON for older records), leaving color in bits 0–2 and the
32-byte record and flash sectors unchanged. Settings v22 stores the last-exit
MIDI octave shift plus 3 in bits 4–6 of the final byte (codes 0–6 map to −3..+3).
Bit 7 and octave code 7 remain invalid in v22; older versions default to octave
zero and retain their existing reserved-bit validation. Settings v23 uses bit 7
to select Peak Limiter (0) or LM-Maximizer (1), independently of dynamics presets
and bypass metadata. Old records default to Peak Limiter. Color, auto scale and
all MIDI octave codes retain their bits; the record remains 32 bytes. Only a
successful Settings SAVE applies the draft mode and its selected working preset
to both the master and an allocated source-only USB OUT engine, preserving
shared Dynamics bypass.
REC activation and held notes remain volatile.

The controller runs only in the CPU0 foreground, ahead of screen/modal input.
It uses the existing USB MIDI queue, two 27-bit wanted/sent masks, seven
absolute encoder positions and a pending-CC mask. At most eight USB packets
are attempted per service, with old-channel/old-pitch releases before note-ons
and CCs. Each encoder tracks direction and elapsed time independently. Slow
detents change its 0–127 position by one; continued turns below 80/35 ms per
detent use steps of two/three. First turns and reversals use one. Delta clamping
before multiplication/addition handles extreme deltas safely. Entry restores
the last-exit position bank and octave without emitting an unsolicited CC burst. Under backpressure,
pending CCs coalesce to the latest position; exiting REC or reconnecting drops
pending sends while retaining positions. Normal REC exit snapshots the seven
positions and octave and requests an immediate foreground settings commit, bypassing the
750-ms debounce. An unchanged snapshot skips the write. Failed writes retain the
snapshot and retry with the existing five-second backoff, even if live positions or octave
change after re-entry. Settings SAVE keeps the last-exit bank unless it disables
MIDI or changes its channel while active; that successful commit includes the
current positions and octave. Turning encoders and pressing OCT performs no flash writes.
The octave shift is bounded to -3..+3; pressing both OCT buttons resets it.
The sent-note bank retains its original
channel and octave until all releases are accepted, including rapid shifts with
a full USB queue. Octave LED timing runs in the foreground without invalidating
the display: solid at one octave, 500 ms blink phases at two, 200 ms at three.
The active REC indicator composites FONT_L red text centered at (120,160), with
10 px padding and a 90% opaque black background (10% scene). Seven smaller padded value cells use
the physical encoder layout in a three-row/four-column grid above it. Composition
uses the existing canvas, without an image buffer or refresh timer. Mode changes
and value changes replay the current view through a clipped canvas transfer;
only the affected area reaches the LCD. Ordinary dirty strips and screenshot
replay composite the same overlays from fresh underlying pixels. Entry requires
`plat_usb_connected()`: rejection produces two foreground REC pulses and a
one-second two-line USB/DISCONNECTED overlay, without starting MIDI or saving.
`plat_midi_try_out` returns acceptance under the platform lock, so a full queue
cannot make the app forget an unsent release. There is no allocation, display
work or MIDI processing in audio/decoder callbacks. The updater's foreground
hook releases notes with a bounded drain before switching firmware.

LDAC uses the pinned Apache-2.0 Open Vela/Sony fixed-point decoder on CPU1,
producing signed 24-bit stereo at 44.1/48 kHz. CPU0 validates RTP/frame headers,
bounded fragmentation, fragment timestamps and sequence order before publishing
immutable complete frames. Timestamp-only steps between ordered complete
packets remain diagnostic; decoded frame counts drive PCM time. Actual loss
resets overlap history at the next accepted packet; decode errors publish timed
silence. The common 24-bit FIR runs on the same
worker, with no CPU0 fallback or callback allocation.
