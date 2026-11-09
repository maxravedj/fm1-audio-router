/* SPDX-License-Identifier: MPL-2.0
 * AC791N register-only DSP helpers. No MMIO, heap or interrupt manipulation.
 */

	.section .text.aptx_dot2,"ax",@progbits
	.balign 4
	.globl aptx_dot2
	.type aptx_dot2,@function
aptx_dot2:
	/* Four argument registers, 64-bit result in r1:r0. Consume both products
	 * before any load: the wide multiply/load erratum forbids spilling here. */
	r3_r2 = r3 * r2 (s)
	r1_r0 = r1 * r0 (s)
	r0 = r0 + r2
	r1 = r1 + r3 + c
	rts
	.size aptx_dot2, .-aptx_dot2

	.section .text.fm1_aptx_fir24_span,"ax",@progbits
	.balign 4
	.globl fm1_aptx_fir24_span
	.type fm1_aptx_fir24_span,@function
fm1_aptx_fir24_span:
	/* r0: aligned stereo high words; r1: stereo low bytes; r2: coefficients;
	 * r3: interpolation fraction [0,1023]; stack: positive count, out[4].
	 * A span never crosses the PCM ring end. Each output is one <=45-tap
	 * partial, whose absolute coefficient sum is <=50573. Therefore neither
	 * signed 32-bit lane can saturate, including any intermediate prefix.
	 * mul2 accumulates independent 16x16 products into two 32-bit registers;
	 * it is NOT a widened scalar multiply or a memory-MAC instruction. */
	[--sp] = {r15-r4}
	r8 = [sp+48]
	r4 = 0
	r5 = 0
	r6 = 0
	r7 = 0
.Laptx_fir_tap:
	r10 = h[r2+0] (s)
	r9 = h[r2+192] (s)
	r9 = r9 - r10
	r9 *= r3
	r9 = r9 + 512
	/* Exactly C's signed truncation toward zero for division by 1024. */
	r11 = r9 >>> 31
	r11 = uextra(r11, p:0, l:10)
	r9 = r9 + r11
	r9 >>>= 10
	r10 = r10 + r9
	r12 = [r0++=4]
	r13 = h[r1++=2] (u)
	r15_r14 = unpackb(r13) (u)
	/* Packed low/high lanes are L/R; accumulator registers are likewise. */
	r5_r4 += r12.h,r12.l *|* r10.l,r10.l (ssat)
	r7_r6 += r14.h,r14.l *|* r10.l,r10.l (ssat)
	r2 = r2 + 2
	if (--r8 != 0) goto .Laptx_fir_tap
	r0 = [sp+52]
	[r0+0] = r4
	[r0+4] = r5
	[r0+8] = r6
	[r0+12] = r7
	{r15-r4} = [sp++]
	rts
	.size fm1_aptx_fir24_span, .-fm1_aptx_fir24_span
