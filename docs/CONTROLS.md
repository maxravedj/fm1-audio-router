# Controls

## Navigation

| Control | Action |
| --- | --- |
| HOME | Return to Home; press again to cycle meters → waveforms → spectrum. |
| GLO | Open the mixer. |
| EDIT | Open Settings. SELECT chooses a row; ALGORITHM edits its draft value. |
| SAVE in Settings | Save and apply the draft. Red SAVED remains until leaving or editing. |
| FX | Open the last FX page; subsequent presses alternate EQ and limiter. |
| Hold FX for 1 s | Toggle the complete master FX chain. |
| SEL on an FX page | Toggle that individual effect; its LED is bright when enabled. |
| PLAY | Toggle MASTER mute from any page. Green means unmuted, red means muted. |

The shared status bar shows USB IN, Bluetooth/codec, FX and USB OUT state, with
optional CPU and estimated battery percentage. Bluetooth is red disconnected,
yellow pairing, green connected. FX is gray off, yellow with both effects
bypassed, green with an effect active. CPU shows first/second core workload;
it includes UI/LCD waits, rather than instruction utilization alone.

## Home visualizers

The meter page shows USB IN, BT IN, USB OUT and MASTER; SELECT does nothing.
On waveform/spectrum pages, SELECT cycles those sources in that order.
Both visualizers have a horizontal selected-source stereo meter, held peak
and source footer.

- Waveforms: K1 adjusts the visible window from 8 seconds to 4 ms. MASTER
  auto scale is optional and display-only; it changes neither audio nor meters.
- Spectrum: K1 sets minimum displayed dB, K2 maximum, K3 decay rate.
  The footer shows MIN/MAX/rate. Forty logarithmic bands have held peak lines.
- During an accepted AAC session, graph views yield their memory to decoding
  and the display uses meters. The selected visualizer returns after disconnect.
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
Settled global/limiter bypass removes that delay. Changing delay alignment
uses a 5-ms fade down, silent switch and 5-ms fade up; the level dip can be heard.
It is not a true-peak/inter-sample limiter.

PRESET cycles eight EQ presets or eight limiter presets and applies them
immediately. Unsaved edits stay in each working slot until reboot; yellow
text and dim SAVE indicate an unsaved slot. SAVE asks to save; hold SAVE for
1 second to initialize. OCT- cancels and OCT+ confirms (YES for initialize).
Selected slot and bypass metadata are retained independently. Preset data is
written only by confirmation, and presets do not contain bypass state.

## Settings and Bluetooth

Settings include Bluetooth OFF/RECEIVER, codec AUTO/AAC/SBC, MASTER waveform
auto scale, separate waveform/spectrum colors, battery percentage display,
CPU display, USB OUT routing and mute/solo fade time. Fade choices are OFF,
10/50/100/500 ms and 1 s. Colors include red, green, blue, yellow, light gray,
white and orange. Battery percentage is an uncalibrated estimate; no voltage
mode is provided.

Enable Bluetooth RECEIVER and SAVE. Hold OCT+ for one second to pair with
**FM1 BT Receiver**; press OCT+ again to stop discovery. Discovery is off at
boot. AUTO advertises SBC and AAC; forced modes expose only that codec.
Saved devices can reconnect after boot/out-of-range loss. An intentional
source disconnect suppresses FM-1's outgoing paging; incoming connections
remain allowed. Logs help distinguish the two directions.

UI/mixer settings and preset selection persist after a brief idle period.
Settings-page drafts require SAVE; leaving without saving discards them.
Firmware updates retain these only when M-Flasher's erase option is unchecked.
