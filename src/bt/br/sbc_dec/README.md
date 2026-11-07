# SBC receiver decoder

Copied from `tests/bt/ref/sbc_dec/` in fm1-base commit
`a3b5da168bc66ff41fc02b413c48e84f5f118709`. This is AOSP/Open Interface's
Apache-2.0 SBC decoder; original copyright/license headers are retained.
The license text ships at `LICENSES/Apache-2.0.txt`.

Gateway modifications:

- `alloc.c`: replace `<stdlib.h>` with `<stddef.h>`. The allocator partitions
  caller-provided scratch memory and never allocates dynamically.
- `synthesis-sbc.c`: perform the four-subband DCT's left shift as unsigned,
  then cast back, preserving fixed-point bits without shifting negative signed
  integers (undefined in C). The sanitizer fixtures cover four/eight subbands.

Production use is bounded by `bt_receiver_media.c`. SBC negotiation accepts
44.1/48 kHz and all four channel modes; mono duplicates into both outputs.
The focused receiver tests compare decoded PCM against FFmpeg's independent
SBC decoder, including four/eight subbands and several bitpools.
