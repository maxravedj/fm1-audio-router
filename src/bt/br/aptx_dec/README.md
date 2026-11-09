# FM-1 aptX / aptX HD decoder

Vendored from [regularhunter/libfreeaptx](https://github.com/regularhunter/libfreeaptx)
commit `476e2d56b0374c643c50bc37ab5cfee3012e2144` (LGPL-2.1-or-later).
`UPSTREAM-SHA256SUMS` identifies the original files; original authors and the full
license are retained. FM-1 changes are marked in the source:

- Local header inclusion and caller-owned, aligned context initialization.
- `FM1_APTX_NO_HEAP` excludes malloc/free entry points from the target.
- Out-of-line signed 32x32-to-64 multiplies avoid the pi32v2 multiply/load erratum.
  Paired QMF/predictor products share one register-only helper call; all results
  are consumed before any memory loads. The BSP leaf uses only argument/result
  registers, eliminating callee-save traffic on each pair. Target disassembly
  and signed/rail arithmetic checks enforce this.
- The standalone `aptx_dec.c` unit uses the app's O2 / `-fwrapv` flags. Firmware
  section GC removes encoder entry points and unused encoder tables.

CPU0 validates A2DP media and publishes a bounded 4096-byte/16-packet queue.
CPU1 decodes at most 256 stereo samples per job and runs the shared 96-tap FIR.
The compressed ring uses all 4096 bytes, including codewords split across wrap;
packets need no padding, and the worker keeps its storage immutable until done.
Standard aptX uses raw L2CAP codewords; aptX HD uses RTP without an SBC header.
Only 44.1/48 kHz stereo configurations are advertised/accepted. Other aptX
variants (LL, Adaptive, Lossless) are not advertised. Packet loss/backpressure
resynchronizes the predictor; teardown joins CPU1 before freeing its state.

Both codecs retain decoded 24-bit precision: the existing signed 16-bit ring
stores high words, optional low bytes live in codec scratch, and the FIR combines
both exactly before rounding once. SBC/AAC retain their original arithmetic.
The 24-bit FIR has its own O2 unit. Two bounded integer partial sums
avoid widened multiplies in the inner loop; contiguous spans avoid repeated
ring masks and register spills without changing coefficients or output bits.
The target uses a BSP register-lane kernel: packed stereo loads and two parallel
16x16 MACs replace four scalar products/additions, with no memory-MAC or widened
multiply in the tap loop. Its two signed 32-bit lanes cannot saturate: the
absolute coefficient-sum bound holds for every prefix of either half. The host
uses scalar products. Omit only the six exactly-zero outer taps; the independent
oracle still evaluates the original 96-tap convolution. Both retain all 24 bits.
`fm1_remote.py log` shows `BT aptX us` (average/max bounded decode job, count)
and `BT FIR us` (average/max 256-frame FIR block, count) during aptX playback.
These are CPU1 wall times in microseconds; they separate decoding from rate
matching when investigating queue drops or underruns on the device.
No graph suppression is applied to aptX. The linked budget check requires the
full spectrum, maximum USB recording workspace, decoder/queue/low-byte scratch
and the existing 8192-byte guard to fit simultaneously.

`python3 src/tests/run_bt_aptx.py` compares both decoders bit-for-bit against
independent FFmpeg decoding at both rates (accounting for the library's 90-frame
startup delay). ASan/UBSan cover transport bounds, queue pressure, teardown,
24-bit FIR wrap/phase/servo arithmetic and foreground/worker ownership. Target
package checks additionally execute the actual linked native helper listings
against signed-product and full-convolution oracles, including callee-register
preservation and the stack ABI. They require the 17-instruction stereo tap loop
to have no calls or stack traffic. These instruction counts are not measured
cycle counts or proof of sustained hardware playback. The
upstream fixed-point code requires two's-complement wrapping and arithmetic
right shifts; host checks use `-fwrapv` and exclude negative signed left shifts
from UBSan, matching the AAC arithmetic contract. RF interoperability, target
CPU load and listening still require a physical device test.

To replace/relink this LGPL component, modify these sources and run the normal
`src/scripts/setup_build.py`, `build_gateway.py` and `package_gateway.py` steps.
The matching source archive contains the application, library, build scripts
and toolchain setup instructions. No signing key is required; firmware-only
updates can preserve the installed bootloader. Do not flash without a backup
and recovery path. The rest of the project's files retain their own licenses.
