# Current release: 0.0.58 (FX transitions and Bluetooth disconnect)

See [0.0.58 changes and validation](../../../docs/0.0.58/README.md).
Latest firmware: `dist/folder/audio-router.fwsc`.

Limiter activation warms its detector during fade-out, applying the ceiling
only after the silent switch to the limited output. Limiter-only edits retain
unchanged EQ filter histories. The requested 5-ms down/up transition and
zero-delay settled bypass remain; a brief level dip can still be audible.
Successful Bluetooth profile setup retires its initial timeout for that ACL,
allowing normal teardown to retain the manual-disconnect reconnect hold.

USB OUT scratch is allocated at the high end, preserving a contiguous gap for
the spectrum after AAC releases its decoder buffers. Waveform/spectrum footers
retain source, settings and right-aligned peak while graph memory is pending.

All status badges share a 14-pixel height and 2-pixel gap, including outlined
badges and the variable-width Bluetooth codec badge.

The shared FX badge is yellow when the chain is on but EQ and limiter are
both bypassed, green when the chain and at least one effect are enabled, and
gray when the whole chain is off. Filled badges retain black text.

With firmware 0.0.55 or newer, run from the folder where you want the image:

```sh
python3 /path/to/audio-router/src/tools/fm1_remote.py screenshot
```

This saves `fm1-screenshot-YYYY-MM-DD_HH-MM-SS_microseconds.png` (240x240).
Use `--port NAME screenshot` to choose a MIDI device. Needs `mido` and
`python-rtmidi`, as the existing log command; Pillow is not required.
The device pauses display updates for the whole capture/download, then resumes.
Audio and the physical MASTER pot stay live. Using a panel button/key or encoder
interrupts the screenshot and applies that control normally. A lost USB link,
five seconds without a request, or the 30-second overall limit also resumes the
display. All rows depict the same frozen meter, graph and status values.
A failed or incomplete transfer creates no PNG. Capture writes no device flash.


Settings now labels Bluetooth codec and CPU load display explicitly. FX titles
read FX: EQUALIZER and FX: PEAK LIMITER; preset labels stay on the right.

Mixer strips are USB IN / BT IN / USB OUT / MASTER. F3/G3/A3/B3 toggle mute, and
F4/G4/A4/B4 toggle solo. The keys glow dim only on Mixer and light fully when
their switches are active. Red M and yellow S boxes below each fader have an
outline while inactive, or a fill with black text while active. Solos combine
within independent input (USB/BT) and output (OUT/MASTER) groups; mute wins.
PLAY shares the MASTER mute. Only Mixer grays strips silenced by mute/solo;
Home meters retain their usual colors, including while PLAY is muted.
Mixer meters show the applied trim before each strip's own mute/solo, so
they keep showing live signal in gray after the mute fade finishes. OUT
metering on HOME/charts continues to show the actual gated USB send.
The M/S stack sits higher, with S's bottom aligned to the meter bottoms.
All source names, badges and record-route labels use USB IN / BT IN / USB OUT;
compact badges fit alongside both CPU percentages and battery status.
PLAY no longer redraws HOME 1 meter chrome. FX reopens its last EQ/limiter
page when returning from another screen; pressing FX inside FX alternates.

Settings adds "Mute/solo fade time": OFF, 10ms, 50ms, 100ms (default), 500ms,
1s. SAVE applies/persists the draft. All mute/solo gates and PLAY use it,
with continuous reversible ramps. Input gates precede the sum/FX; raw record
taps ignore them, post-FX routes follow them. OUT gates the final USB send
independently of MASTER/PLAY. MASTER metering precedes its gate. Live mute/solo
switches auto-save and restore at boot. Settings v15 is 24 bytes, with v1-v14
migration; AFX v4/presets and flash sectors are unchanged.

MASTER uses a monotone cubic gain curve: 0% exact silence, 10% -60 dB,
50% -30 dB, 75% -15 dB, 100% unity. It approaches zero smoothly without a
finite dB floor. Four generated cubic segments run once per audio block in
fractional Q12 units; the mixer reads back the same gain. Generate or verify
coefficients with `python3 src/scripts/generate_master_curve.py [--check]`;
`--formula` prints a percent-to-dB plotting expression. USB/BT/OUT faders
remain -100..0 dB plus OFF, with existing acceleration/trim ramps.

Mixer has a single shared left dB scale matching Home 1: 0, -6, -12, -24,
-36, -48, -60. The four strips fit beside a 20-pixel scale gutter, keeping
16-pixel bars, 2-pixel channel gaps and 5-pixel gaps to the faders. Peak/gain
values remain centered above their respective meters/faders, with names below.
The scale is static. Fader tracks are 116 pixels tall to make room for M/S;
source names center under the stereo pair and gains above their faders.

EQ and limiter each have eight independent presets. PRESET cycles slots and
applies their working settings; yellow labels indicate unsaved edits. Working
edits survive slot changes until reboot. SAVE opens a confirmation on release;
hold SAVE for one second to confirm Initialize instead. OCT- cancels, OCT+
commits. Only confirmed operations save preset parameters. Selected positions
and global/individual FX on/off auto-save independently; presets never change
EQ/limiter bypass. SAVE lights dim when the current preset has unsaved edits.
Save confirmation offers CANCEL / SAVE; Initialize offers CANCEL / YES, with
OCT- / OCT+ on the row below. Boot loads the selected saved presets. Legacy
v1/v2 FX controls migrate to Preset 1; v3 preset banks retain all slots and
move selected effect enable flags into independent metadata. K2 retains
pass slope; PRESET no longer has the alternate slope assignment.

Settings keeps a stable six-row viewport until selection crosses an edge.
Spectrum shows MIN/MAX/decay above source/peak; K3 clockwise runs SLOW to MID
to FAST, retaining the existing saved decay IDs and actual rates.

The limiter uses a curved anticipatory gain ramp over 128 samples (~2.9 ms).
Both global FX bypass and limiter-only bypass remove this added delay, with
5 ms fade-out, a silent timing switch and 5 ms fade-in. EQ alone adds no delay.
The release/input/ceiling controls remain unchanged. AFX v4 still stores
the preset bank in the existing FX sectors.
This is a sample-peak limiter; hardware sound and CPU checks remain pending.

Branch `waveform_optimisation` retains temporary, RAM-only USB profiling and
controlled comparisons. Instrumentation has some overhead. On a HOME screen,
`python3 src/tools/fm1_remote.py profile 10` records per-stage timings. See the
release notes for LCD, capture and CPU2-polling comparisons and their limits.
The profiler is inactive by default; hardware measurements are pending.

0.0.43 fixes the profiler's IRQ-state handling. The old flash helper always
enabled interrupts on restore, including inside an audio ISR or a platform
lock. The corrected profiler independently preserves ICFG bit 9. The 0.0.42
lock maxima and scheduling-sensitive timing comparisons need a fresh capture.

The following sections describe earlier releases.

# FM-1 Audio Gateway

Historical 0.0.25 controls (superseded by the release notes above):

HOME order remains 1 meters / 2 waveforms / 3 spectrum; SELECT cycles sources.
First FX press opens the EQ graph and five bottom band cards. Press FX again
for the separate stereo limiter meters and red gain-reduction line; another
press returns to EQ. HOME returns to the monitor view; EDIT opens settings.

On EQ, SELECT cycles Chain / EQ-post / Bands 1-5. ALGORITHM changes band type;
K1 frequency, K2 gain, K3 Q/resonance, K4 band on/off. PRESET chooses
12/24/36/48 dB/oct for low/high passes (inactive for other types). Pass cards
show slope in place of gain, since gain has no effect on pass filters. Chain
row K1/K4 enables the whole chain; EQ row K1 sets post, K4 toggles EQ.
Positive turns enable, negative bypass. The chain starts OFF on fresh installs.

On Limiter, K1 Input, K2 Release, K3 Ceiling, K4 on/off. Meters follow MASTER
output before the PLAY fade and gray on mute; reduction is before MASTER.
Hidden limiter telemetry stops. The EQ graph calculates configured curves only
when edited, with the yellow combined curve flat when chain/EQ is bypassed.
There is no extra FFT or spectrum capture behind this screen.

FX edits save after 750 ms, with 5-second failure retries and immediate SAVE
retry. AFX v2 is 62 bytes; v1 migrates with original 12-dB passes and preserved
settings. The storage map, general v7 settings, codec preferences, MASTER and
100-ms mute fade remain unchanged. Fixed 128-frame (~2.9-ms) delay remains.

Build: `python3 src/scripts/build_gateway.py --host`, then
`python3 src/scripts/build_gateway.py`. Firmware replaces
`dist/folder/audio-router.fwsc` after successful builds. Tests:
`tests/run_master_fx.py` and `tests/run_monitor_codec.py` under this app folder;
preview scripts are `waveform_preview.py` then `fx_preview.py`. The host tests
use the actual DSP/app/mixer; no hardware CPU or analog sound claim follows.

The sections below record earlier milestones; current behavior is specified
above and in the latest release notes.


Silent app render, existing USB playback and optional SBC Bluetooth receiver
mixed afterward at the platform MASTER gain, blue USB stereo waveforms and
input/post-MASTER stereo meters. Bluetooth receiver hardware validation is pending.
Knob 1 adjusts the untriggered waveform from 8 seconds to 4 ms. Updates
refresh every 16 ms (approximately 60 FPS), drawing changed rectangles.
CPU workload is at the top; the visible time span is at the bottom.
The workload meter covers audio, scan/USB interrupts and foreground UI work,
including LCD waits, with nested interrupts counted once. Idle polling and
uninstrumented platform housekeeping are excluded. The old counter measured
audio rendering alone. Real FM-1 performance requires on-device observation.
HOME returns to main from settings, or cycles main modes while already there:
mode 1 has waveforms, meters and CPU; mode 2 has meters and CPU only;
mode 3 has 40 yellow input-frequency bars (20 Hz..20 kHz), pale yellow peak lines, frequency labels
and CPU, without meters. Its 4096-point stereo FFT runs on the foreground
thread at most once per 33 ms. Opposite-polarity stereo does not cancel.
On spectrum, K1 sets decay (FAST/MID/SLOW: 48/24/12 dB/s), K2 sets MAX
(top level 0..-60 dBFS in 1-dB steps), and K3 sets RANGE (15/30/60/90 dB).
The left dB scale follows MAX/RANGE; the bottom is MAX minus RANGE.
Defaults are MID, MAX 0 and RANGE 90. Peak lines hold for 750 ms, then fall
at half the selected bar rate. No numeric spectrum peak values are shown.
The frequency footer has compact numeric values, without a Hz suffix.
Levels/peaks stay in absolute milli-dBFS, so display zoom does not change decay.
Fractional FFT-bin overlap keeps narrow bass bands populated; true resolution
remains 10.77 Hz. Hidden spectrum capture and analysis stop as well.
EDIT opens Settings. SELECT chooses the row (currently Bluetooth only),
ALGORITHM changes OFF/RECEIVER, and SAVE persists and applies the draft. SAVE
is dim only on Settings. A successful save shows red SAVED at the bottom until
leaving or changing a setting. Leaving discards unapplied drafts. HOME and EDIT
are dim when inactive and bright on their own screen.
When Bluetooth is enabled, OCT+ is dim: hold one second to enable discovery as
AUDIO ROUTER. OCT+ becomes bright during pairing; another press cancels discovery.
Known bonded sources can reconnect while discovery is off. Discovery starts off
at boot. The platform radio crash guard still suppresses an automatic start
after a radio crash; SAVE explicitly retries it. Mode 2 shows a filled green BT badge when connected, red otherwise.
SBC is the only codec; 44.1/48-kHz mono, dual, stereo and joint stereo are accepted.
The bounded decoder runs in the foreground, with filtered clock adaptation in
ALNK0. USB and Bluetooth mix before MASTER, the MASTER meter and mute fade.
Waveforms, INPUT meters and spectrum still display USB input only.
Waveform history/drawing buffers (59,368 bytes) and spectrum capture/PCM/FFT/
twiddle buffers (98,304 bytes) are allocated only in their respective main view.
Mode 2 and Settings release both to reusable SRAM, after detaching interrupt
producers. Re-entry allocates fresh history; neither view retains the other
view's large buffer. Allocation failure leaves capture off and shows an error.
See `src/docs/bt/BT-RECEIVER-PAGE.md` for 0.0.14 incoming connection corrections
and pairing diagnostics; Android interoperability still needs device retesting.
Hidden waveform/meter work stops in both the producer and UI paths; returning
to waveforms starts fresh history. Knob 1 adjusts zoom only while it is shown.
PLAY mutes/unmutes with a 100-ms fade, a green button when unmuted, and a red
button with gray MASTER meters while muted, on either screen.
The meter tap precedes this fade. MASTER uses a -60..0 dB audio taper:
half travel gives about 3.2% amplitude, with exact silence at zero.
Meter bars are 14 pixels wide,
with a right-aligned scale and INPUT / MASTER captions underneath.
Above each stereo pair, one numeric dBFS readout follows the higher held L/R
peak (750-ms hold, then decay). A reading below the -60 dBFS floor shows --.
Zoom, mute, main mode and spectrum RATE/MAX/RANGE save automatically 750 ms after the last change and restore on
boot, using CRC-checked A/B records at 0xFC000/0xFD000. Version 4 extends the record to 16 bytes; v1/v2/v3 records preserve their
existing settings and default Bluetooth to OFF. Bluetooth address/bonds use the
CRC A/B pair at 0xDA000/0xDB000. Saves continue during
playback. The gateway uses an opt-in SRAM runtime, with all runtime code and
constants copied at boot. Flash erase/program polls allow audio/USB/scan
interrupts to run, avoiding the old interrupt stall and DMA silence during
autosave. Ordinary store readback uses the plain XIP data window with IRQs on.
The physical MASTER position is always read from the knob. Failed saves retry
after 5 seconds.
Booting muted initializes the output gain to zero before audio starts.
The boot splash shows centered AUDIO / ROUTER / version lines.
Both USB audio directions remain enabled because playback depends on them.
USB playback uses explicit asynchronous feedback (`usb_audio_out_feedback = true`):
EP3 IN reports the FM-1 audio consumption rate to the computer, and the FM-1
reads successive playback frames without the old two-point interpolation.
The format remains 44.1-kHz stereo/16-bit; USB console stays disabled for EP3.
Reconnect USB after installing so the host sees the updated USB descriptors.
Host mixing/rate conversion can still occur before samples reach the FM-1.

From the project root:

```sh
python3 src/scripts/setup_m1.py
python3 src/scripts/build_gateway.py
python3 src/apps/fm1-audio-gateway/tests/run.py
python3 src/apps/fm1-audio-gateway/tests/run_package.py dist/folder/audio-router.fwsc
```

Every successful device build refreshes `dist/folder/audio-router.fwsc` at a
fixed path. Host-only builds leave that file alone.

Version 0.0.13 adds the experimental SBC receiver, Settings save/apply, pairing
controls and the mode-2 connection badge. Run the focused receiver checks:

```sh
ASAN_OPTIONS=detect_leaks=0 python3 src/apps/fm1-audio-gateway/tests/run_bluetooth.py
```

See [the 0.0.13 notes](../../../docs/0.0.13/README.md) for source audit, memory,
host evidence and pending physical receiver checks.

Version 0.0.12 adds the dB scale and DEQ2496-style spectrum window controls.
Run the focused spectrum/settings checks:

```sh
ASAN_OPTIONS=detect_leaks=0 python3 src/apps/fm1-audio-gateway/tests/run_spectrum.py
```

See [the 0.0.12 notes](../../../docs/0.0.12/README.md) for validation and control
details. Asynchronous USB feedback remains enabled as described in
[0.0.11](../../../docs/0.0.11/README.md).

See [the 0.0.5 guide](../../../docs/0.0.5/README.md) for Arch packages, pinned
dependencies, test evidence, licensing and M-Flasher instructions.
No build or test command here accesses a physical device.

0.0.15 adds Peripheral clock tracking throughout incoming connections and
captures the last failed session's timing/SSP/LMP metadata independently of scan
retries. `python3 src/tools/fm1_remote.py log` reads it over USB-MIDI. The user
observed 0.0.14 connecting and reaching local SSP state 9 before timing out;
0.0.15 requires a fresh Android pairing test. Detailed fields, tests and limits
are in `src/docs/bt/BT-RECEIVER-PAGE.md`.

0.0.16 completes the reverse authentication required after fresh incoming SSP.
The user's 0.0.15 log shows a 30-second authentication timeout with good receive
timing, not an RX timeout. The fresh-pairing phone fixture now waits for our
reverse challenge and checks both authentication directions and the resulting
encryption key. Run the focused regressions without unchanged page/LMP tests:

```sh
python3 src/apps/fm1-audio-gateway/tests/run_bt_page.py --auth-only
```

See `docs/0.0.16/README.md` for build evidence and the pending Android retest.


0.0.17 requests encryption immediately after successful incoming authentication,
rather than waiting for Android to request it. The FM-1 remains the Peripheral;
Android negotiates key size and supplies EN_RAND. Crossed encryption requests
use the Central's transaction and tolerate its collision rejection in either
receive order. A real rejection still fails the session. The log now includes
`BT encryption requested`; state 10 waits for its answer and state 11 waits for
Central key-size/start PDUs. The focused sanitizer suite covers fresh P-192
pairing, saved bonds, both encryption transaction IDs, collisions and key
publication. See [0.0.17 notes](../../../docs/0.0.17/README.md) for build evidence.
Physical Android pairing/audio remain pending a device retest.


0.0.18 optimizes the Bluetooth PCM resampler after the user confirmed Android
playback with 0.0.17 and reported about 87% CPU. It removes per-tap division,
uses bounded 32-bit partial sums and cancels the gain's redundant scale factors.
The 96-tap filter, coefficient interpolation, clock servo and output samples
remain identical. Focused tests compare every fractional phase at both rates
against frozen 0.0.17 arithmetic under ASan/UBSan:

```sh
python3 src/apps/fm1-audio-gateway/tests/run_bt_pcm.py
```

The target assembly has no divisions in its sample/tap loops; the three
occupancy-servo divisions run once per block. See
[0.0.18 notes](../../../docs/0.0.18/README.md) for evidence and limits. The new
on-device CPU percentage must be measured while playing the same material
with the same display mode.

0.0.19 starts the decoder-first AAC-LC work described in `bt-aac.md`. The normal
firmware still advertises/plays SBC. Mode 2 adds a green negotiated-codec badge;
AAC rendering is exercised with a host mock until the transport is implemented.
An unconfigured or disconnected link clears the codec label.

Saved-bond reconnects now initiate authentication after LMP setup if Android
does not send AU_RAND. Crossed authentication retains the Central's transaction
and correct ACO. SDP/basic L2CAP control can proceed before encryption; AVDTP
and media still require encryption. Disconnect resets session state and ignores
old queued packets without overwriting its failure record.

The portable LC-only decoder supports raw access units at 44.1/48 kHz,
mono/stereo, with caller-owned workspace. Independent FFmpeg comparisons and
bounded malformed-input checks run on host; a separate pi32v2 compile/link
probe reports resource costs. Neither is a physical AAC playback test:

```sh
python3 src/tests/run_bt_aac.py
python3 src/scripts/probe_bt_aac_target.py
python3 src/apps/fm1-audio-gateway/tests/run_bt_page.py --auth-only
```

See [0.0.19 notes](../../../docs/0.0.19/README.md) and
[AAC audit](../../../docs/0.0.19/AAC-AUDIT.md) for evidence, memory implications,
licensing and the remaining RTP/LATM/PCM integration. Android reconnect needs
an on-device retest; no firmware was installed during development.

0.0.20 enables AAC-LC alongside SBC on separate A2DP Sink endpoints. The AAC
receiver parses RTP/LATM (no ADTS), reassembles bounded fragmented elements,
and feeds the same PCM ring/rate-matching path. Both 44.1/48 kHz and mono/stereo
are accepted; mono duplicates to both outputs. AAC uses meters/CPU with a green
AAC badge, temporarily releasing waveform/spectrum buffers. The prior view
returns when the AAC session is released. Saved settings are not rewritten
by this automatic switch. The UI host simulator still mocks codec state;
`python3 src/tests/run_bt_aac_transport.py` tests the actual AAC decoder,
RTP/LATM and L2CAP dispatch. See `docs/0.0.20/README.md` for limitations and logs.

0.0.21 adds a Bluetooth codec row in EDIT settings: AUTO, AAC, SBC. SELECT
chooses the settings row, ALGORITHM changes its draft, and SAVE persists and
applies it. Changing the codec disconnects the current stream so it can be
negotiated again. AUTO advertises both; forced modes expose only the selected
endpoint. Settings and the selected monitor restore on boot.

On main modes 1 and 2, SELECT chooses USB, BT or MASTER for monitoring only.
Mode 1 changes its waveform and input meter to that source. Mode 2 retains
USB, BT and MASTER stereo meters, highlighting the selection below the bars.
BT is measured before MASTER, and MASTER after gain but before mute/fade.
The audible USB+BT mix is unchanged. AAC continues to use the three-meter view.
Its configuration callback releases waveform/spectrum scratch synchronously,
before more media can be drained from the radio queue.

With Bluetooth RECEIVER enabled, the FM-1 now pages bonded sources after boot
and link loss, leaving scan gaps between bounded attempts. OCT+ pairing cancels
an outgoing attempt. Outgoing links authenticate, enable encryption and open
the AVDTP signalling channel. This is host-model tested; physical power-cycle
and out-of-range automatic reconnect still need testing. See
[0.0.21 notes](../../../docs/0.0.21/README.md) and run:

```sh
python3 src/apps/fm1-audio-gateway/tests/run_monitor_codec.py
python3 src/apps/fm1-audio-gateway/tests/run_bt_page.py
```

## 0.0.22: monitoring display refinements

SELECT now selects USB, BT or MASTER in waveform and spectrum modes only.
Mode 2 SELECT is inactive; USB, BT and MASTER remain visible together with
uniform meter segments. Mode 1 has full-width waveforms; modes 1 and 3 show
one thin horizontal stereo meter at the bottom for the selected source.
The held maximum/scale sit above its bars, with the source at the top and the
waveform window at the top right. MASTER waveforms have bounded automatic
display gain so quiet output stays visible; metering remains absolute dBFS.
Bluetooth is named **FM1 BT Receiver** in discovery and remote-name replies.
AAC retains its temporary meters-only display and scratch release behavior.
See [0.0.22 notes](../../../docs/0.0.22/README.md) for tests and previews.

## 0.0.23: waveform settings and cyclic sources

EDIT adds Master waveform auto scale ON/OFF and six waveform colors (red,
green, blue, yellow, light gray, white). SELECT chooses a row, ALGORITHM edits
its draft, SAVE persists/applies; defaults remain ON/BLUE. Settings v6 keeps
the same 16-byte record and migrates v1-v5. SELECT on every HOME mode now
cycles USB/BT/MASTER in either direction; Mode 2 highlights selection while
retaining all three meters. The audible mix is unchanged.
See [0.0.23 notes](../../../docs/0.0.23/README.md) and the
[master FX design plan](../../../docs/plans/MASTER_FX_PLAN.md). FX remains
unimplemented.
