/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 RAM-runtime entry, adapted from felucca-bsp crt0.S.
 * Copy runtime code/constants before calling any C code; no packed boot.
 * The ordinary startup still initializes data, BSS, guards and peripherals.
 * FM-1 standalone app entry. The SPL maps app.bin
 * at XIP 0x02000120 and jumps to its first byte with r0 = boot-parameter block
 * (0x01C7FE08, inside the vector table we are about to own). The SPL expects
 * `goto +4; rts` as the first instruction. Before anything else the core state
 * the SPL left is saved for the boot probe; then interrupts off, our stacks. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	r1 = icfg
	r2 = psr
	cli
	r3 = 0x1C07F00		/* STATUS (debug area) */
	[r3 + 0x20] = r1	/* icfg as handed over */
	[r3 + 0x24] = r2	/* psr */
	[r3 + 0x28] = r0	/* boot-parameter pointer */
	r1 = sp
	[r3 + 0x2C] = r1
	r1 = ssp
	[r3 + 0x30] = r1
	sp = _ustack_top
	ssp = _sstack_top
#ifdef FM1_CPU1
	/* A warm reset can leave CPU1 running: hold it before overwriting SRAM. */
	r0 = fm1_ram_prestart
	call r0
#endif
	r0 = _rt_load
	r1 = _rt_start
	r2 = _rt_end
2:	if (r1 == r2) goto 3f
	r3 = [r0+0]
	[r1+0] = r3
	r0 = r0 + 4
	r1 = r1 + 4
	goto 2b
3:	r0 = _data_load
	r1 = _data_start
	r2 = _data_end
4:	if (r1 == r2) goto 5f
	r3 = [r0+0]
	[r1+0] = r3
	r0 = r0 + 4
	r1 = r1 + 4
	goto 4b
5:	csync
	r0 = fm1_cstart
	call r0
	goto .
