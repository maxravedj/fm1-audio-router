# Controls

## Navigation

| Control | Action |
| --- | --- |
| HOME | Return to Home; press again to cycle meters → waveforms → spectrum, skipping unavailable graphs. |
| GLO | Open the mixer. |
| EDIT | Open Settings. SELECT chooses a row; ALGORITHM edits its draft value. |
| SAVE in Settings | Fully lit with unsaved changes; saves and applies the draft. Red UNSAVED becomes green SAVED until leaving or editing. |
| FX | Open the last FX page; subsequent presses alternate EQ and limiter. |
| Hold FX for 1 s | Toggle the complete master FX chain. |
| SEL on an FX page | Toggle that individual effect; its LED is bright when enabled. |
| PLAY | Toggle MASTER mute from any page. Green means unmuted, red means muted. |

The shared status bar shows USB IN, Bluetooth/codec, FX and USB OUT state, with
optional CPU and estimated battery percentage. Bluetooth is red disconnected,
yellow pairing, green connected. USB OUT is gray when OFF, green when enabled
with USB connected, and red when enabled with USB disconnected.
FX is gray off, yellow with both effects
bypassed, green with an effect active. CPU shows first/second core workload;
it includes UI/LCD waits, rather than instruction utilization alone.

## Home visualizers

The meter page shows USB IN, BT IN, USB OUT and MASTER; SELECT does nothing.
On waveform/spectrum pages, SELECT cycles those sources in that order.
Both visualizers have a horizontal selected-source stereo meter, held peak
and source footer.
Meter graphics cover -60 to 0 dBFS. Numeric held peaks keep their one-decimal
readout below that floor, down to the smallest nonzero 24-bit sample (about
-138.5 dBFS). Silence shows `--` once the held peak has decayed. The same numeric
range is used on Home meters, Mixer and the limiter.

- Waveforms: K1 adjusts the visible window from 8 seconds to 4 ms. MASTER
  auto scale is optional and display-only; it changes neither audio nor meters.
- Spectrum: K1 sets minimum displayed dB, K2 maximum, K3 decay rate.
  The footer shows MIN/MAX/rate. Forty logarithmic bands have held peak lines.
- During an accepted AAC session, graph views yield their memory to decoding
  and the display uses meters. The selected visualizer returns after disconnect.
- LDAC gives decoding priority over graphs. Spectrum (and waveforms if needed)
  falls back to meters when SRAM is insufficient. HOME skips unavailable graphs
  instead of visiting a duplicate meter page. Available waveforms remain usable
  while paused; disconnect restores normal graph availability.
- USB OUT monitoring shows silence until the host records and routing is enabled.

## Mixer

Four strips run USB IN / BT IN / USB OUT / MASTER. K1/K2/K3 adjust the first
three faders; MASTER follows the physical pot. Encoders accelerate. Faders
cover -100..0 dB plus a separate OFF position. MASTER uses a smooth curve:
0% silence, 10% -60 dB, 50% -30 dB, 75% -15 dB, 100% unity.

While the mixer is visible, F3/G3/A3/B3 toggle strip mute and F4/G4/A4/B4
strip solo. Inputs and outputs form separate solo groups. Multiple solos are
allowed; mute takes precedence. Boxes and panel LEDs show active states.
Meters continue showing post-fader signal before their mute/solo gates, in
gray when suppressed. Gray styling is confined to the mixer. PLAY and the
MASTER mute box control the same state.

## EQ and limiter

The EQ page has five band cards and a combined response graph. SELECT cycles
five bands and post-EQ gain. ALGORITHM selects bell, low/high pass or low/high
shelf. For a band: K1 frequency, K2 gain (or 12/24/36/48-dB/oct pass slope),
K3 Q/resonance. With post gain selected, K1 adjusts it. Knobs do not toggle
bypass. Individual bypass grays that effect's graph, independently of global FX.

The limiter page has K1 input gain, K2 release, K3 ceiling, post-limiter meters
and a red gain-reduction line. It is a linked sample-peak limiter with 128-frame
lookahead (about 2.9 ms), anticipatory curved attack and exponential release.
Slow release-knob turns adjust by 1 ms; fast turns accelerate up to 30 ms per detent.
Settled global/limiter bypass removes that delay. Changing delay alignment
uses a 5-ms fade down, silent switch and 5-ms fade up; the level dip can be heard.
It is not a true-peak/inter-sample limiter.

PRESET cycles eight EQ presets or eight limiter presets and applies them
immediately. Unsaved edits stay in each working slot until reboot; yellow
text and fully lit SAVE indicate an unsaved slot. SAVE asks to save; hold SAVE for
1 second to initialize. OCT- cancels and OCT+ confirms (YES for initialize).
Selected slot and bypass metadata are retained independently. Preset data is
written only by confirmation, and presets do not contain bypass state.

## Settings and Bluetooth

Settings include Bluetooth OFF/ON, a Bluetooth codecs checklist, MASTER waveform
auto scale, separate waveform/spectrum colors, battery percentage display,
CPU display, USB OUT routing, mute/solo fade time and MIDI controller options. Fade choices are OFF,
10/50/100/500 ms and 1 s. Colors include red, green, blue, yellow, light gray,
white and orange. Battery percentage is an uncalibrated estimate; no voltage
mode is provided.

Fresh settings default to Bluetooth ON with SBC and AAC selected,
MASTER waveform auto scale OFF, green waveforms, yellow spectrum, battery and
CPU displays ON, USB OUT OFF and a 50-ms mute/solo fade. The global FX chain
starts bypassed, with EQ and limiter individually enabled. Existing saved
preferences take priority over these defaults.

## MIDI controller

Set **MIDI: CONTROLLER** in Settings and SAVE. **MIDI controller channel**
selects channel 1–16 (default 1); **MIDI highlight C** defaults OFF. MIDI itself
defaults OFF. REC is dim on every screen when the controller is available.
Press REC to activate it (bright REC); press again to return to normal controls.
The active toggle starts OFF at every boot, independently of the saved settings.
Entry requires USB to be connected. Otherwise REC flashes twice and a two-line
**USB / DISCONNECTED** message appears for one second.

While active, all seven encoders send USB MIDI absolute CC messages instead of
editing the displayed screen. MASTER still controls audio volume. Buttons keep
their screen/navigation functions, and note keys take priority over Mixer mute/solo.
Turn controller mode off with REC to edit Settings using the encoders.
Active controller mode shows a red **MIDI** overlay on every screen, centered
one-third from the bottom, with a 10% transparent black background. Smaller, padded values
above it follow the physical encoder layout: SELECT in row 1, column 3;
PRESET and ALGORITHM in row 2, columns 2 and 3; K1–K4 across row 3.

| Encoder | CC |
| --- | ---: |
| SELECT | 20 |
| ALGORITHM | 21 |
| K1 / K2 / K3 / K4 | 22 / 23 / 24 / 25 |
| PRESET | 26 |

Choose **Absolute** interpretation in the receiving software. Each encoder has
an independent 0–127 value, initially 64. Each clockwise detent
increases it, each anticlockwise detent decreases it; values
clamp at 0 and 127 without wrapping. Leaving MIDI with REC saves the positions
for the next entry and reboot. Turning knobs does not write flash; power loss
before leaving MIDI restores the previous saved positions. Slow turns step
by one per detent. Continued turns in the same direction step by two below
80 ms per detent, or three below 35 ms. The first turn, direction changes and
pauses return to fine control; each encoder tracks its speed independently.
Turning an encoder sends its latest position.
The 27 keys send F3–G5 (MIDI notes 53–79) at the default octave, velocity 127, on the selected
channel. They are dim when idle and bright when held. Highlight C keeps the
C4/C5 keys bright whether idle or held, without generating extra
notes. **OCT−/OCT+** transpose by one octave per press, up to three octaves in
either direction. Press both OCT buttons together to reset to zero. Both buttons
are dim at zero. The shifted direction is bright
at one octave, blinks with a one-second cycle at two, and a 400 ms cycle at three;
the other direction stays dim. Held notes are released at their old pitch before
restarting at the new pitch. The shift survives REC toggles and resets on boot.
In confirmation dialogs, OCT−/OCT+ keep their cancel/confirm functions. Bluetooth
pairing with OCT+ is available after leaving MIDI mode.

The USB MIDI port/interface and jacks are named **MIDI**; hosts that combine
product and port names display **FM1 Audio MIDI**. The USB audio
product remains **FM1 Audio**. Note-offs take priority over CCs and remain
pending if USB's output queue fills. Changing channel releases old-channel
notes first; USB reconnection discards stale CCs. Audio processing is unchanged.

If macOS still shows **Felucca**, quit MIDI applications, disconnect the FM-1,
and open Audio MIDI Setup → Window → Show MIDI Studio. Remove the offline
Felucca device, reconnect the FM-1 and use Rescan MIDI, then reopen the apps.
This refreshes the saved host configuration; it does not erase FM-1 settings.
See Apple's [MIDI Studio controls](https://support.apple.com/guide/audio-midi-setup/midi-studio-window-ams1001/mac).

## Bluetooth

Enable Bluetooth ON and SAVE. Hold OCT+ for one second to pair with
**FM1 BT Receiver**; press OCT+ again to stop discovery. Discovery is off at
boot. The Bluetooth codecs value reads SELECT; open it with SEL. All five
codecs (SBC, AAC, aptX, aptX HD and LDAC) are visible together. Turn SELECT to move
between them; press SEL to toggle each checkbox. SEL lights fully on the codec row
and while the dialog is open. OCT- cancels the checklist; OCT+ OK copies it into
the Settings draft and closes it. Both OCT buttons light fully in the dialog.
The main Settings SAVE applies and persists the draft.
HOME, EDIT, GLO or FX cancels an open checklist. At least one codec must
remain checked; use Bluetooth OFF to disable the receiver. Older AUTO/AAC/SBC
settings migrate to the corresponding selection. Changing the advertised list
disconnects an existing stream so the source can negotiate again. aptX/aptX HD
keep waveform and spectrum views available; AAC retains its meter fallback.
LDAC supports stereo 44.1/48 kHz with memory-dependent visualizer availability.
The checkboxes advertise capabilities; the source chooses a mutually supported
codec. Keep AAC or SBC checked for sources that cannot use aptX/aptX HD/LDAC.
The Bluetooth badge shows `BT` without a codec, or `BT SBC`, `BT AAC`,
`BT aptX`, `BT aptXHD` or `BT LDAC` for the negotiated codec when connected.
Saved devices can reconnect after boot/out-of-range loss. An intentional
source disconnect suppresses FM-1's outgoing paging; incoming connections
remain allowed. Logs help distinguish the two directions.

UI/mixer settings and preset selection persist after a brief idle period.
Settings-page drafts require SAVE; leaving without saving discards them.
Firmware updates retain these only when M-Flasher's erase option is unchecked.
