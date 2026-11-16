# Bluetooth receiver and multipoint

Two source devices can keep separate encrypted ACL and A2DP sessions. Only
one session owns the CPU1 decoder, exact 96-tap resampler and PCM rings.
An accepted AVDTP START from the other source joins/resets the old worker,
selects the new source and queues AVRCP PASS THROUGH Pause press/release to
the old one. Late packets from the old stream never enter the decoder. A
rejected START does not change ownership. Pause depends on the source honoring
AVRCP; even if it does not, only the selected source reaches the audio mix.

Pairing uses the unused slot without replacing the current connection. A
one-second **OCT+** hold starts pairing; another press stops discoverability.
Two occupied slots do not evict a source to make room. Stored bonds retain
mutual authentication and encryption requirements. Manual-disconnect and
missing-key holds follow peer addresses, independent of which slot later
reconnects. A peer-closed AVDTP signalling channel also holds automatic profile
and ACL reconnection, even when its encrypted ACL remains up. Peer-closed AVRCP
is not reopened automatically; an accepted incoming AVDTP signalling open
releases that peer's hold and permits AVRCP again. An accepted new stream also
permits a new AVRCP connection. AVDTP CLOSE/ABORT and media
channel teardown alone do not create a hold. Explicit pairing clears the
volatile holds; range loss does not create one. Bluetooth OFF stops the radio
and joins the decoder before reset.

Each slot owns its baseband frame, RX/TX buffers and counters, E0 key, clock
offset, SSP state, control TX FIFO, L2CAP assembly and profile state. Queued RX
events carry a slot and session generation, rejecting events from retired
sessions. The active codec determines graph admission. A standby AAC/LDAC
configuration cannot free the playing source's workspace or display buffers.

Control replies reserve TX room before changing state. A full queue leaves
the complete command in existing reassembly storage for foreground retry;
compound L2CAP commands resume at their unprocessed suffix. Outgoing connection
transactions are latched only after successful enqueue. Drawing checkpoints
retain their media-only/FIFO behavior and cannot retry signalling or switch
decoder ownership. A terminal LMP DETACH at the RX FIFO head needs no reply
and can retire its session despite a blocked control/TX queue; it must not be
misclassified later as a range-loss timeout.

PCM starvation fades the last audible stereo sample to silence over 64 output
frames (about 1.45 ms), waits for the existing 1536-frame output watermark,
then fades in over 64 frames. It introduces no allocation, worker wait or
extra delay in uninterrupted playback. This conceals a discontinuity; it
cannot reconstruct missing audio or establish RF throughput.
Before declaring starvation, the audio callback refreshes CPU1's published
write cursor: a block completed during that callback must not cause a false
underrun and unnecessary fade/reprime. This does not wait for CPU1, add latency
or change the watermark.

## Radio implementation and evidence

BR/EDR permits a device to participate in different piconets by time division
([Core Baseband, scatternet operation](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/br-edr-controller/baseband-specification.html)).
The implementation schedules two complete controller frames, rather than
guessing how the 16 exchange-table indices map to future slots.

Register facts were checked against the pinned AC79 SDK's `bredr_bd_frame_enable`,
`link_task` suspend/resume and link-resume slot timer logic:

- SDK revision `ffbb2a8d925682ba6880d643a83a63ee0691ec09` in
  [amitv87/fw-AC79_AIoT_SDK](https://github.com/amitv87/fw-AC79_AIoT_SDK/tree/ffbb2a8d925682ba6880d643a83a63ee0691ec09).
- `cpu/wl82/liba/btctrler.a` SHA-256
  `9ef4262444b49171c4a5eab906e3939b1d4943350ac4993c18d8474d01d773de`.
- Enabling a frame updates all 16 exchange entries; resume selects parity
  from the link clock offset and arms an eight-slot alarm. The SDK uses
  software suspend/resume for radio time sharing.

The code is written against those register facts; no SDK controller object
or implementation is linked or copied. Controller frames start at byte 368
with a 180-byte stride. Peer RX pairs begin at 1088/1768 and 2448/3128;
DM1 control TX pairs at 4272/4292 and 4312/4332. Total DMA storage is 4352
bytes; source-only DH5 TX buffers are absent. Only one outgoing page handshake
uses the shared FHS transmit buffer at a time.

During playback the playing frame receives 160-slot slices; an encrypted idle
peer or an empty, non-pairing scan receives eight slots. With the six-slot
inhibited guard in each direction, the nominal playing airtime is 88.9%, up
from 74.1% with the original 80/16 split. The ordinary idle/empty visit leaves
playback off air for 20 slots (12.5 ms), down from 28 slots (17.5 ms). These
figures include the guards; they do not predict real RF throughput or losses.

When fewer than 1024 prepared output frames remain (about 23 ms at 44.1 kHz),
the playing slice can extend up to 320 slots (200 ms) to refill. This bound
keeps an underfeeding source from starving another client's START. The radio
IRQ reads only permanent PCM cursors, never an allocated codec workspace.
Negotiation, pairing and queued standby control use 16-slot visits; with no
stream, both frames keep the original 16-slot service. A new audio owner
gets the playing priority at the next scheduling decision. All switches keep
the six-slot inhibited guard for a maximum-length exchange.
Page/FHS handshakes retain their frame until complete. Foreground may reset
only an inactive/drained frame, with radio IRQs masked during the check and
frame update. Simultaneous closes stay inhibited until foreground restores
a scan frame. IRQ wrappers restore the foreground session selection.

These slice weights and guard timing are implementation choices requiring
hardware validation, not timings measured on an FM-1. The host register model
does not emulate hopping, packet ARQ, E0 hardware or RF collisions.

AVRCP discovery advertises a basic Controller, category 1, using PSM `0x0017`;
no absolute-volume Target or browsing service is advertised. Pause is operation
`0x46` with separate press/release transactions. Protocol references:
[A2DP 1.4.1](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/a2dp_v1-4-1_1752513648/A2DP_v1.4.1/out/en/index-en.html),
[AVRCP 1.6.3](https://files.bluetooth.com/wp-content/uploads/dlm_uploads/2024/10/AVRCP_v1.6.3.pdf).

## Validation

Focused ASan/UBSan tests cover independent security keys/queues, last-START
arbitration, rejected/late media, AVRCP messages, standby suspend/detach,
simultaneous closes, stale generations, bounded SDP and full control queues.
Ten-second register models cover both audio slots, idle/empty/pairing peers,
clock wrap, airtime and absence bounds, low-PCM extensions and handover.
Disconnect tests cover both AVRCP/AVDTP close orders on both slots, explicit
incoming reconnect, and DETACH with blocked control replies.
The real AAC/FIR worker tests use the production radio policy with clock and
register stubs, paced packets at 44.1/48 kHz, 12 initial clock phases and an
old client's longer control visit around one second after stream startup,
with bounded synthetic job times. A deterministic publication test completes a
CPU1 block during CPU0 consumption, including cursor wrap, and checks that no
false underrun or sample discontinuity occurs. These are software models,
not measurements of radio/decoder timing on the device.
The real threaded decoder/FIR pipeline tests recovery ramps, reference PCM,
watermarks and teardown without allocation/waits in the audio callback.
Linked native checks retain the 8192-byte scratch guard, stack limits and
SBC/aptX plus spectrum plus maximum USB recording budgets.

Physical acceptance still requires two sources: pair the second during
playback; alternate START/SUSPEND and confirm audible ownership and remote
Pause; exercise both disconnect orders, range loss, manual disconnect and
power cycles; repeat for every codec with USB recording and graph navigation.
Capture `python3 src/tools/fm1_remote.py log` before disconnecting on failure,
including both source models/OS versions and selected codecs. See
[debug commands](../DEBUG.md). No hardware validation is implied by host tests.
