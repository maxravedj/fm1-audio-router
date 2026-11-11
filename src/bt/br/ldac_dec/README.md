# LDAC decoder port

This fixed-point module supplies the live LDAC receiver on CPU1. It supports
stereo 44.1/48 kHz and preserves 24 decoded bits through the shared resampler.
Transport, graph fallback and measured SRAM budgets are documented in
[docs/LDAC.md](../../../../docs/LDAC.md).

## Provenance

Source: [open-vela/external_libldac](https://github.com/open-vela/external_libldac/tree/6e0474c954103ff7aefb7908b97e65522c9208fa),
revision `6e0474c954103ff7aefb7908b97e65522c9208fa`. The `inc/` and `src/`
trees, LICENSE, NOTICE, METADATA and MODULE_LICENSE_APACHE2 are retained under
`upstream/`. This contains Sony's decoder as well as its encoder; only the
fixed-point decoder enters the adapter build. The encoder is used on the host
to generate test fixtures.

The upstream source is Apache-2.0. Its separate [NOTICE](upstream/NOTICE)
states that certification is required for using LDAC in products and links to
[Sony's information](https://www.sony.net/Products/LDAC/aosp/). That notice is
retained alongside the source license; this port makes no certification claim.

`UPSTREAM-SHA256SUMS` describes the original pinned files. The host test
reconstructs and verifies those originals before building its reference. Three
upstream files have marked adaptations:

- `src/fixp_ldac.h`: out-of-line helpers consume wide multiply results together
  with rounding/shifting before returning to memory loads, following the pi32v2
  multiply/load workaround. Integer results are unchanged.
- `src/struct_ldac.h`: a build guard omits unused hosted I/O, heap and math
  headers for this fixed-point, freestanding decoder build.
- `src/tables_sigproc_fixp_ldac.c`: a build guard removes the inverse-transform
  table branch for 88.2/96 kHz; the adapter rejects those rates before use.

The adapter supplies wrapping integer absolute value, disables all libc heap
fallbacks and uses the upstream initialization bump allocator in an exactly
sized, zeroed arena. It does not call the upstream heap-owned Bluetooth API.

## Contract

`ldac_dec.h` accepts one complete sync-header-bearing codec frame at a time.
It supports 44.1/48 kHz stereo, with 128 stereo PCM frames per codec frame,
returning signed 24-bit PCM in interleaved int32 slots. RTP/media headers,
packet frame counts, fragmentation, timing and loss handling are implemented by
`../bt_receiver_ldac.c`. The stateless `fm1_ldac_frame_size()` header validator
lets CPU0 check frame boundaries without touching decoder state. Dual-channel,
mono and higher sampling rates are rejected for now.

Allocate `fm1_ldac_dec_size()` bytes aligned to eight bytes before starting the
worker, then initialize with the negotiated sample rate. Output needs 128
stereo frames aligned to four bytes. Decoder storage, input and output must be
disjoint. No call allocates memory. A single owner must serialize all LDAC
operations, including initialization/reset, since upstream also initializes
shared table pointers. Stop/join the worker before reset/destroy/free.

Before live decoding, CPU1 calls `fm1_ldac_dec_check()` once per allocation.
Two embedded upstream-encoded frames use the existing output scratch; the first
primes overlap and the second has independent left/right reference checksums.
The probe resets history afterwards and never publishes its PCM. It allocates
nothing. A failed check suppresses live audio until teardown/reallocation.

Argument errors leave decoder history/output untouched. Invalid frames and
decode errors reset overlap history; callers must discard output on any error.
The upstream bit reader can peek past the declared payload length; the adapter
first validates the three-byte header, copies the exact payload into its own
zero-padded 1,026-byte buffer, and requires exact consumption after decoding.
It never passes the caller's unpadded frame into that reader.

Build with `_DECODE_ONLY`, `_32BIT_FIXED_POINT` (set inside the adapter),
`-O2 -fwrapv -ffp-contract=off -ffunction-sections -fdata-sections` and section
garbage collection. The FM-1 compile also uses `-mcpu=r3 -mfprev1` and the
application's `compat/string.h`. Runtime text and tables must reside in SRAM
as enforced by the full firmware build.

## Validation

```sh
python3 src/tests/run_bt_ldac.py
python3 src/tests/run_ldac_target.py
```

The first command needs a host C compiler and ASan/UBSan, and runs in the
Bluetooth CI suite. It generates fixtures with the pinned unmodified fixed-point
encoder and compares every output bit against the pinned unmodified decoder:
44.1/48 kHz, HQ/SQ/MQ and continuous quality changes, silence, tones, alternating
rails and independent noise. It also runs the real RTP/FIFO/CPU1/24-bit FIR
pipeline against those fixtures, including fragmented and bundled packets.
It also exercises truncation, invalid headers/configuration, reset, aliases,
capacity and deterministic malformed/mutated payloads. Shift-base sanitization
is excluded for upstream's intentional signed fixed-point shifts; wrapping
arithmetic is enabled. This oracle checks the port against upstream, not against
an independently implemented decoder.

The second command needs the pinned FM-1 toolchain from setup. It builds only
an isolated SRAM-addressed probe, never a flashable image. Its ignored report is
`build/tests/ldac/target/budget.json`; disassembly and optimized IR are alongside
it. The probe checks static stacks, unresolved/heap/software-double imports and
the wide-multiply/load hazard. A narrow model executes both linked arithmetic
helpers against 8,000 signed/rail/shift cases and checks register preservation
and product consumption before loads/return. It does not emulate silicon faults.
The embedded check vectors are separately verified with the unmodified upstream
decoder. The probe's tiny memcpy/memset stand-ins are included in
the reported text/stack cost. Full firmware linking must repeat the real stack,
SRAM and callback ownership checks. Neither test establishes RF interoperability,
listening quality or sustained FM-1 CPU headroom.
