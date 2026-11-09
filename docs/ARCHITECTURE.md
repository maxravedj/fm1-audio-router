# Runtime and audio architecture

The application is `src/apps/fm1-audio-gateway/`, built on the modified fm1-base
platform and vendored felucca-bsp. Its authoritative API is `src/include/plat.h`.
Generic Dinghy examples do not describe every detail of this runtime.

## Signal paths

```text
USB playback ─ trim/gate ─┐
                         ├─ sum ─ 5-band EQ/post ─ limiter ─ MASTER ─ mute ─ codec
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
stop; waveform/FFT scratch is detached from IRQs before being freed.

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
- CPU1: bounded compressed SBC/AAC/aptX/aptX HD decoding and the exact 96-tap resampler.
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

AAC workspace takes priority over waveform/spectrum buffers. Accepted AAC
configuration synchronously releases graphs before allocation. They return
fresh after disconnect; failed visible graph allocation retries without
clearing the continuing meter/footer. Persistent SBC/aptX/USB route allocations
use the high end of scratch to preserve a contiguous graph gap.

## FX and timing

EQ uses cascaded trapezoidal SVFs with a stable-bank transition for edits.
Unchanged EQ paths copy their warm histories on unrelated limiter/post-gain
edits. Five bands offer bell/pass/shelf types; pass slopes cascade 1–4 stages.
The limiter links stereo sample peaks and uses a monotonic lookahead deque,
finite curved anticipatory gain ramp, existing exponential release and final
integer ceiling guards. No audio callback allocation occurs.

Delay is 128 frames only while the limiter output is active. Global/limiter
bypass switches at silence between 221-frame down/up envelopes. Detection may
warm during fade-out, but a pending ceiling must not clamp the old bypass
path. The active limited output is constrained through fade-in and steady
operation. A changing time alignment/level dip is not universally inaudible.

## Persistence and recovery

CRC-checked A/B objects retain panel calibration, application settings, saved
FX presets and bond metadata. Settings payload schema is 16 (24 bytes); FX
preset schema is 4 (456 bytes). These are storage format numbers, independent
of manually chosen firmware tags. Keep their migrations, not DSP history.

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

Host tests provide protocol/register, pixel, sample and lifecycle evidence.
Actual RF timing, power-loss recovery, CPU headroom and listening remain
separate device-validation tasks.

aptX and aptX HD use the pinned LGPL libfreeaptx decoder on CPU1. An optional
low-byte PCM ring keeps all 24 decoded bits through the rate matcher and pre-FX
mix; SBC/AAC keep their original 16-bit arithmetic. See
[src/bt/br/aptx_dec/README.md](../src/bt/br/aptx_dec/README.md) for transport,
source/relinking instructions and validation limits. Settings v16 stores the
codec checkbox mask in the existing codec byte, retaining the 24-byte record
and all v1-v15 migrations. The advertised list applies only on successful SAVE.
