# Incoming Classic receiver: page timing facts

Audio Router 0.0.14 addresses Android reporting “Can't communicate with AUDIO
ROUTER” after discovering the name. Discovery alone establishes neither an ACL
connection nor authentication. These are implementation defects found by code
inspection; the complete Android failure has not been traced on physical radio.

## Provenance

Protocol reference: Bluetooth Core, Vol 2 Part B, the
[baseband specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/br-edr-controller/baseband-specification.html),
particularly peripheral page response and connection establishment.

Hardware facts were inspected in JieLi AC79 AIoT SDK `btctrler.a`, pinned mirror
commit `ffbb2a8d925682ba6880d643a83a63ee0691ec09`:
[controller archive](https://github.com/amitv87/fw-AC79_AIoT_SDK/blob/ffbb2a8d925682ba6880d643a83a63ee0691ec09/cpu/wl82/liba/btctrler.a).
Archive SHA-256: `9ef4262444b49171c4a5eab906e3939b1d4943350ac4993c18d8474d01d773de`.
`bredr_link.c.o` and `bredr_frame.c.o` contain LLVM bitcode, inspected with the
pinned pi32v2 compiler's `-S -emit-llvm -x ir` mode. Relevant symbols:
`page_scan_resume`, `frame_bitoff_adjust`, `page_scan_step_2`,
`bredr_link_rx_irq_handler`, and register accessor functions.

Copyright (c) 2024–2026 Zhuhai Jieli Technology Co., Ltd.; AC79 SDK,
Apache-2.0. See `LICENSES/Apache-2.0.txt` and `bsp/NOTICE`. The original own-stack
implementation was extended using register/timing facts; no vendor controller
code is linked or copied as implementation. The new receiver code remains
MPL-2.0. This is reverse engineering, not a claim of independent clean-room work.

## Register and frame facts used

- Page-scan format 5 uses own address/BCH and TX payload headers 2. Existing
  generic frame defaults use a different TX header and must be overridden.
- On an incoming ID: enable both exchange parities, use receive window 440,
  set format 7; next TX link control 0x4010 and alternate 0x4000.
- RXBIT low ten bits are a receive timestamp, not a transmit BITOFF value.
  During page response, timestamps below 88 map to timestamp+224; 88..399 map
  to timestamp-88; 400..1023 map to timestamp-400, masked to ten bits.
- After FHS, program the Central address/BCH, LT_ADDR, clock offset and format
  3 immediately. The clock offset is FHS CLK shifted by one minus receive slot
  clock, plus one when RXBIT low ten bits are below 88. Arithmetic wraps at
  the 27-bit slot-clock boundary. An alarm two slots later narrows the window.
- Register 0x20028 supplies the receive-window baseline. The connection step
  sets it to 46 and the frame window to 66 (baseline+20). The signed transmit
  bit offset is RXBIT-68-window/2. A negative value wraps by 625; the slot
  offset carries once if the earlier FHS timestamp was at least 88.
- Stale RX status bit 7 is set before waiting for new connection packets.
  NULL/POLL can arrive without RX payload-DMA bit 0: good header status must
  establish/refresh the connection even when no buffer is full.

These facts are isolated in the BSP and `bt_receiver_page.c`. The latter is
included unchanged by the device and the host register-model test. No low-level
register access was added to app code. The frame structure was moved without
layout changes to `fm1_bredr_frame.h` so both use the same checked layout.

## Evidence and limitations

`tests/bt_page_test.c` covers 2,048 timestamp/TX-parity/clock-wrap combinations,
page/FHS format/address/clock handover, payload-free first POLL, ordered
connection-before-LMP delivery, stale IRQ rejection, invalid FHS, page timeout
and inquiry/page/cancel behavior. ASan/UBSan pass. Fresh peripheral SSP through
encryption, Central TID, wrong-DHKey rejection, and bonded encryption are also
checked by `tests/bt/bt_ssp_test.c`.

These tests execute the real C state machine against memory-backed registers;
they do not simulate modulation, hopping, hardware DMA, RF delays or Android's
controller. No physical pairing success is claimed. Remaining evidence needed:
first Android pairing, bonded reconnect, SBC playback and sustained RF operation.

Main-thread radio logs now retain page/FHS counters and phase, ACL connection,
SSP failure opcode/reason, encryption/setup, remote detach and timeout stage.
No log formatting runs in the interrupt handler. Collect with the existing
read-only `python3 src/tools/fm1_remote.py log` after reproducing a failure.
Phases are 0 inquiry, 1 page scan, 2 page response, 3 FHS ACK, 4 awaiting first
connection packet, 5 connected. Do not use the unrelated source-side `bta`
commands to diagnose this receiver.

## 0.0.15: post-confirmation timeout

User device evidence on 0.0.14: Android presents pairing confirmation. The log
then contains `BT incoming ACL connected`, `BT LMP setup complete`, timeout at
phase 5 and security state 9 (`BTP_AUTHED`). That local state means an AU_RAND
was processed and SRES queued, not that Android accepted SRES or that encryption
started. The old timeout line does not distinguish 4-second RX supervision from
30-second authentication deadline, and repeated scan lines overwrite earlier
SSP events. This narrows the failure but does not establish its complete cause.

The incoming implementation set BITOFF only on FHS handover. A Peripheral must
continue following its Central; the existing outgoing Central driver does not
need this tracking. The missing path was checked against `__rx_adjust_clkoffset`
in the same pinned controller archive. Additional register facts used:

- For the ordinary connection window 66, signed BITOFF is RXBIT[9:0]-101;
  wrap negative values by 625 and mask to ten bits.
- Compare the old normalized BITOFF with the new one. If old <300 and new
  >old+324, increment slot offset. If old >324 and new <old-324, decrement slot
  offset. This handles phase wrap without treating normal drift as a full slot.
- Update clock offset, BITOFF and exchange parity (clock offset bit 0) on
  received timing. The SDK uses its Peripheral role/clock-adjustment enable;
  the incoming-only path applies this on good new/retransmitted packets,
  including payload-free POLL/NULL, once per received slot. Bad status does
  not update the phase. The short IRQ path only updates integers/table words.

0.0.15 implements this ordinary, narrow-window tracking. It does not implement
sniff/hold/sleep-clock wide-window reacquisition or role switching, none of which
are advertised by this receiver. No controller implementation was copied.
A synthetic radio-window model runs +/-100-ppm drift for 400 seconds, with
phase/slot wrap and rejected-header gaps. An entire device-session register
model also holds the authentication state across a 12-second POLL confirmation
pause and checks Central encryption, actual TX queue publication and key/flags
ordering. These tests do not prove RF interoperability or physical clock margin.

Another defect was found in rejection handling: shared SSP sets FAILED on
LMP_NOT_ACCEPTED but returns zero to let the caller print it. The incoming
caller checked FAILED only when that return was nonzero. It now captures the
rejection and closes immediately. The outgoing branch is unchanged.

### Retained diagnostics from `fm1_remote.py log`

The same USB-MIDI `log` command now appends dedicated receiver rows, independent
of the 32-line shared radio log. A failure snapshot plus its last 16 received LMP
metadata records survive subsequent inquiry/page retries. Snapshot is RAM-only:
a new failed ACL session replaces it, and firmware reboot clears it. No link
keys, nonce/public-key payloads, device addresses or audio enter this trace.
Rows are bounded to the existing 43-byte USB log payload:

| Prefix | Following decimal fields |
| --- | --- |
| `BT now` | current phase, local SSP state, encryption flag |
| `BT rx` | good received slots, bad statuses, clock updates |
| `BT tx` | LMP handovers to hardware, queued PDUs, last handed LMP opcode |
| `BT fail` | failure reason, detail, reserved zero; reason zero = no failure |
| `BT stage` | failed phase, failed SSP state, uptime milliseconds |
| `BT age` | connection age milliseconds, time since last good header, zero |
| `BT frx` | failed-session good slots, bad statuses, clock updates |
| `BT ftx` | failed-session LMP handovers, queued PDUs, last handed opcode |
| `BT sync` | failed-session normalized BITOFF, clock offset, phase carries |
| `BT last` | last raw RXBIT, RX status word, last received LMP opcode |
| `BT trace` | milliseconds since connection, opcode/TID, SSP-before>SSP-after |

Reasons: 1 RX silence >4000 ms; 2 authentication not encrypted after 30000 ms;
3 SSP failure (detail is Bluetooth reason); 4 remote detach (detail is reason);
5 local OFF/re-pair; 6 unknown source outside pairing mode. Local SSP state 9
means SRES produced/waiting for encryption, 11 means key-size negotiation,
13 encrypted, 14 failed. Extended LMP opcodes are `(127<<8)|extended_opcode`.
Timing/counters are captured under a short IRQ lock; formatting and USB output
are in the main thread. Scan progress uses one compact folded line so it no
longer floods the shared log with three alternating rows per second.

## 0.0.16: fresh-pairing mutual authentication

The user's 0.0.15 snapshot distinguishes the failure: reason 2, connection age
30001 ms, last good header 4 ms ago, 1384 received slots and timing updates,
zero bad statuses. The final received LMP is AU_RAND (11) at 1465 ms, state
7 -> 9; the final handed TX opcode is SRES (12), with no PDUs left queued.
There is no received encryption-mode request. This attempt reached the
authentication deadline with receive timing still working; RX supervision
did not expire. TX handover itself does not establish Android's receipt.

The receiver omitted the reverse legacy authentication at the end of fresh
SSP. After answering the Central's AU_RAND it marked itself AUTHED and waited
for encryption. The fresh-pairing phone must first receive the responder's
challenge and return its own SRES. The prior host phone fixture incorrectly
skipped this step and immediately requested encryption, hiding the omission.

The normative sequence is in Bluetooth Core 6.1 Vol 2 Part C
[4.2.7.4 and 4.2.1.1](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/br-edr-controller/link-manager-protocol-specification.html):
SSP link-key calculation is followed by standard mutual authentication when
Secure Connections is not supported, and a received AU_RAND must be answered
before sending a local challenge. Transaction IDs follow Part C 2.4. Retain
the successful authentication's ACO for encryption as described by Vol 2 Part H
[3.2.5 and 5.1](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/br-edr-controller/security-specification.html).

0.0.16 queues SRES followed by a fresh AU_RAND within the incoming Central's
pairing transaction. It waits in state 8 (AUTHING), validates the returned SRES
and transaction ID, then enters state 9 retaining the reverse authentication's
ACO. It remains the Peripheral and SSP responder, and waits for Central-driven
encryption; it does not issue its own encryption-mode request. Incorrect SRES
fails the session without saving a bond. Already bonded reconnect behavior
and outgoing Central behavior are preserved.
A new incoming IO-capability request clears the session's saved-key mode;
this also covers Android forgetting a bond which remains stored on the FM-1.
The complete P-192 fixture runs that fresh-pairing case with an older key present.

The complete P-192 fresh-pairing fixture now waits for the reverse challenge
before permitting encryption, verifies the second ACO and final Kc, and
checks all outgoing transaction IDs. The receiver integration fixture verifies
actual TX queue order and key/flag publication, keeps waiting through a
12-second response delay with good headers, and checks successful bond creation
only after encryption and no bond on wrong SRES. Negative SSP tests cover
truncated/wrong-TID responses and early encryption requests. The new regression
initially produces seven failures on 0.0.15 and passes on 0.0.16 under ASan/UBSan. The
`--auth-only` test option skips unchanged page-timing and LMP-response suites.
On successful reverse authentication, the main-thread log adds
`BT mutual authentication complete`. Physical Android interoperability remains
pending device retest.


## 0.0.17: request encryption after Android authentication

The user's subsequent Android log confirms `BT mutual authentication complete`,
then a state-9 timeout at 30001 ms with the last good header only 14 ms ago.
The retained LMP trace ends in SRES (12), 8 -> 9. No encryption-mode request
arrives. This confirms progress past the 0.0.16 reverse-authentication fix;
it does not establish why Android does not start encryption.

The incoming receiver now initiates encryption after verified authentication.
It queues `LMP_ENCRYPTION_MODE_REQ(1)` once, with a new Peripheral transaction
ID of 1, and logs `BT encryption requested`. Android remains Central and must
send KEY_SIZE_REQ and START_ENCRYPTION_REQ using that same transaction ID.
The Peripheral accepts the proposed size, derives Kc from Android's EN_RAND
and the retained ACO, and enables hardware encryption before handing its
START acceptance to the radio. No key is saved before encryption succeeds.

This follows Core Vol 2 Part C
[2.4, 2.5.1 and 4.2.5.1–4.2.5.3](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/br-edr-controller/link-manager-protocol-specification.html).
Either LM may initiate the encryption-mode procedure; the Central still
negotiates size and starts encryption. All encryption PDUs retain the mode
request's transaction ID. If requests cross, the Central's transaction wins.
A late 0x23 collision rejection of our outstanding TID-1 mode request is
recognized without aborting the Central's procedure. A real rejection is
still terminal. After mode agreement, an unexpected AU_RAND is rejected with
0x24 without overwriting the ACO or active encryption transaction.

Tests exercise fresh P-192 pairing including an older saved key, saved-bond
reconnects, mode acceptance without a Peripheral key-size/start request,
wrong transaction IDs, both collision receive orders, seven- and sixteen-byte
keys, ACO protection, terminal rejection and actual queue/key publication.
The new receiver regression fails on 0.0.16, then passes under ASan/UBSan.
The test runner uses `--auth-only`; unchanged page timing and long audio tests
are not rerun. Firmware logs: state 10 means waiting for mode acceptance,
state 11 means waiting for Central key-size/start, state 13 means encrypted.

This change targets Android's post-authentication stall. L2CAP/SDP routing,
radio timing, audio and UI are unchanged; in particular the existing policy
still defers L2CAP until encryption. Host models cannot establish successful
Android pairing, RF/DMA behavior or audio playback. Those require device retest.

## 0.0.19: initiate saved-bond authentication on reconnect

The user confirmed Android pairing/playback after the encryption fix, then
reported silent reconnect failures. The new trace reaches LMP setup with local
SSP state 7 (saved key, PAIRED), then remains there for 27,121 ms. RX has 1,112
good slots, no bad statuses and no late/header gaps; there is no received
AU_RAND. An earlier successful encrypted session is followed by DETACH reason
19 and a misleading authentication timeout. These observations locate the
stall before authentication; they do not identify Android's internal policy.
The selected user-supplied excerpt is in `logs/0.0.19/android-reported.log`.

For a saved bond, the receiver now sends one AU_RAND after LMP_SETUP_COMPLETE
using its own Peripheral transaction ID 1. A valid SRES proves the stored key,
retains the challenge's ACO and triggers the existing encryption request.
If the Central's authentication crosses ours, its TID-0 transaction wins;
late SRES/collision rejection for the abandoned challenge cannot overwrite
the Central's ACO. Fresh SSP mutual authentication is retained.
This follows Core Vol 2 Part C
[2.4, 2.5.1 and 4.2.1.1](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/br-edr-controller/link-manager-protocol-specification.html).

SDP and basic L2CAP control are now answered before encryption, allowing
service discovery while link security progresses. AVDTP connection requests
without encryption receive the L2CAP security-block result; AVDTP data/media
remain gated even for an existing channel. SDP's security exemption is
specified in Core Vol 3 Part C
[5.2.2](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/host/generic-access-profile.html).
Detach immediately resets the phase/security/profile/PCM state. Old queued
ACL/LMP events are ignored outside a live connection, and a later poll cannot
replace the detached session's retained failure with a false timeout.

The ASan/UBSan authentication subset exercises a saved-bond phone that never
sends AU_RAND, a 12-second delay with good POLLs, wrong-TID/incorrect SRES,
both authentication-collision orders, early SDP with ACL fragmentation,
unencrypted AVDTP rejection, fresh mutual authentication, stale queued events,
retained detach diagnostics and repeated session resets. Command:
`python3 src/apps/fm1-audio-gateway/tests/run_bt_page.py --auth-only`.
Expected progress includes `BT saved bond authentication requested`,
`BT saved bond authentication complete`, `BT encryption requested` and
`BT encryption active`. A crossed Central challenge may take the alternate
authentication path. Physical Android reconnect validation is still pending.


## 0.0.21 outgoing bonded reconnect

The receiver additionally pages saved bonded sources after startup/link loss.
The Central page/FHS/POLL sequence is adapted from bt_page.c using its MPL-2.0
option. Central links retain their own clock; Peripheral clock following still
runs for incoming links. Authentication/encryption and outgoing AVDTP signalling
channel setup are exercised by the full run_bt_page.py suite, including retry
timeouts, scan gaps, reverse-auth ordering and pairing cancellation.

A fresh ACL resets setup_sent. DETACH drain ignores RX page interpretation.
BT fail reason 7 denotes an outgoing audio-profile configuration timeout.
See docs/0.0.21/README.md for reconnect policy, Android source references and
hardware-test limitations. Physical automatic reconnect has not been verified.


## 0.0.34 profile setup and intentional disconnects

See `docs/0.0.34/README.md` at the project root for the supplied macOS log
analysis, specification references, exact diagnostic row meanings and tests.
The receiver accepts peer Flush Timeout and explicit Basic-mode RFC options.
The USB log additionally exposes profile state, per-direction channel readiness,
last AVDTP response/error, last incoming PSM result, configuration rejection,
and the outgoing reconnect hold. These are live per-session metadata; the
retained LMP fault/trace semantics remain unchanged.

Remote 0x13 after an encrypted session holds outgoing saved-source paging
until a successful encrypted manual connection, explicit pairing start, or
receiver restart. Incoming bonded connections remain possible. Unexpected
radio loss, power-on reconnect and unencrypted setup handovers still retry.
The physical reason macOS omits an output remains unconfirmed until retesting
with the new setup rows. Codec configuration alone does not imply OPEN/START.


## 0.0.35 explicit re-pairing with a retained bond

A source that forgot its pairing can reject the receiver's saved-key AU_RAND
with reason 6, PIN or Key Missing. During explicit pairing, do not initiate
that saved-key challenge at LMP setup; allow the source to start fresh SSP.
Still answer a valid saved-key peer challenge. Retain the stored bond until
successful encrypted pairing replaces it, and keep fresh pairing permission
gated on OCT+. Missing-key failures stop automatic paging until pairing, a
successful manual encrypted connection, or receiver restart. This is a
receiver policy change; the shared SSP engine still fails missing-key auth.
See the project-root docs/0.0.35/README.md for the supplied log, regression and
physical retest instructions. Nonzero audio counters may belong to prior
sessions; zero current profile rows do not imply earlier AAC never ran.


## 0.0.36 markerless AAC and complete media diagnostics

The user's Mac reaches encrypted STREAMING with START accepted but silent
meters; Android plays. The previous 26-row USB log cap hid AAC counters behind
a retained trace. Emit up to 36 rows, with the six codec/audio/queue/AAC rows
before the trace. Counters remain cumulative across sessions.

For AAC RTP without a final marker, a timestamp change closes the preceding
assembly only if the full bounded LATM parser validates it. Marked completion
is unchanged. Sequence gaps/SSRC changes cannot finish stale assemblies. The
last unmarked element waits for another timestamp (one-element delay) and is
released on shutdown. Host regressions demonstrate this framing defect and
bit-identical decoded PCM after the fallback; Mac packet framing and physical
playback are not established by the supplied log. See docs/0.0.36/README.md.


## 0.0.37 CPU2 AAC worker

CPU0 validates/assembles RTP and enqueues up to 16 LATM elements in a 16-KiB
compressed-byte ring. CPU1 alone parses/decode/publishes AAC PCM, using a
single outstanding platform job and cross-core PCM barriers. Allocation/free
stays on CPU0; lifecycle reset joins before freeing and before resetting audio
head/tail. No CPU0 decoder fallback. Missing-marker timestamps still use full
LATM validation. The normal log's AAC max-us now measures CPU1 decode; us/mem's
context-live flag and cumulative packet/frame counters keep their meaning.
Both panel CPU readings are measured workload; CPU1 idle polling is excluded.
See docs/0.0.37/README.md for SRAM/stack bounds and device checks. The user
reports macOS playback seems to work on the previous, single-core 0.0.36 build.
