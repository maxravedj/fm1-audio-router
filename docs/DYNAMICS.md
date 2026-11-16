# Dynamics

The second FX page runs one of two stereo-linked sample-peak processors.
**Dynamics mode** in Settings selects **Peak Limiter** or **LM-Maximizer**;
SAVE applies and stores the choice. Peak Limiter is the default for both fresh
and migrated settings. EQ, the physical MASTER gain, mute and USB routing
retain their positions in the signal path.

| Control/behavior | Peak Limiter | LM-Maximizer |
| --- | --- | --- |
| K1 | Input gain, −24..+30 dB | Input gain, −24..+30 dB |
| K2 | Release, 10..1000 ms | Ceiling, −24..0 dBFS |
| K3 | Ceiling, −24..0 dBFS | Unused; display shows AUTO release |
| Lookahead | 128 samples, about 2.90 ms | 55 samples, about 1.25 ms |
| Attack | Finite curved gain ramp | Inverse-gain ramp with retained slew |
| Release | Manual exponential | Automatic fast, medium and long recovery |

Times use the hardware ALNK clock, approximately 44,117.647 Hz. The display
shows post-dynamics stereo levels before MASTER and mute, plus gain reduction.
SEL controls one shared Dynamics bypass state; holding FX bypasses the complete
chain. Peak Limiter and LM-Maximizer each have eight independent preset slots
and a remembered selection. Switching mode applies its selected working preset,
including unsaved edits retained until reboot. SAVE confirmation stores only
that mode's current slot; initialize confirmation resets only that slot.
Presets contain input, ceiling and manual release, never mode or bypass. The
manual release value is retained but ignored in LM-Maximizer. Mode remains a
separate Settings preference. Older shared presets and their selected position
migrate into both banks, which can then be edited independently.

## LM-Maximizer controller

This is an original implementation informed by public-ABI, offline measurements
of the user's LoudMax 1.47 stereo LADSPA plugin with ISP disabled. No LoudMax
implementation, binary, rendered audio or fitted waveform tables are included
in firmware. The temporary research directory remains outside version control
and is not a build dependency. This is not an exact LoudMax recreation.

The controller operates on the larger absolute channel value, normalized by
the ceiling. A monotonic maximum deque looks ahead 55 samples. An inverse-gain
ramp anticipates that maximum and retains attack slew across overlapping
events. The same gain applies to both channels. Shared float and integer
ceiling guards constrain the actual delayed samples during startup, live
parameter changes and steady playback. MASTER follows this stage.

Release combines a fast inverse-gain recovery with two history paths:

- A non-extending, approximately 2.05-ms peak-admission bucket feeds two
  charging poles. Continuous bursts build a medium tail; separated bursts
  leak their charge instead of accumulating forever.
- A sufficiently charged sequence can capture a longer recovery state.
  It progresses through early, middle and late decay on a lookahead-sized
  clock. A larger new transient can start charging without erasing an already
  captured tail.
- Quiet rising input can hold the visible gain, pause hidden recovery, or
  both. Middle-stage age advances independently of such a pause.

The 1.25-ms lookahead, separated release timescales, input-relative hold
classes and multi-stage recovery are supported by the reference measurements.
Arbitrary-input charging, capture admission and their interaction are causal
approximations. The narrow fitted errors of the offline research predictors
are **not** accuracy claims for this firmware. Short-burst tails and complex
peak histories can differ from LoudMax, and there is no ISP/true-peak mode.

## Timing, storage and validation

Both algorithms run on CPU0 in the existing FX callback. They share the delay
ring, maximum deque and an exclusive detector union; there is no second active
dynamics chain for a single route. Source-only post-FX USB OUT retains its own
complete history and follows the selected mode. Processing uses fixed storage
and no callback allocation, waiting, drawing or flash writes.

Settled global or individual dynamics bypass has zero added delay. Mode changes
and bypass transitions fade down for 221 samples, change the tap at exact
silence, then fade up for 221 samples. Each fade lasts about 5 ms; this level dip
can be audible. Warm EQ history is retained for unchanged paths. Dynamics
telemetry does not count that switching envelope as limiting.

Host tests cover all 241 integer ceilings with driven/headroom input, linked
stereo, lookahead deadlines, block-size invariance, timestamp wrap, independent
routes, automatic-release independence from the saved manual value, recovery
recurrences and silent mode transitions. UI tests cover draft/discard/failure/
save/boot behavior, presets, both USB FX routes and settings migrations. Linked
target checks verify SRAM placement, stacks, callback ownership and scratch
budgets. These checks do not establish physical CPU headroom or listening
quality; those still need testing on the FM-1.
