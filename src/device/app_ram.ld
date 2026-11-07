/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Henri David (the packed SRAM) */
/* Audio Router RAM runtime memory map (opt-in ram_runtime = true).
 * Based on the Felucca memory map.
 * Only the bootstrap executes in XIP; runtime code and constants are copied to RAM.
 * SRAM 0x01C00000..0x01C74000 is one region, packed from its bottom, each part right after the one before (no fixed
 * boundaries: an app's RAM code, data and pool share one budget; tools/build.py checks it, keeping 8 KiB spare):
 *   .ram_text  the flash driver and ALL runtime C/assembly code, copied before C startup.
 *              The guard's one window for code outside XIP is exactly this run (_rt_start.._rt_end, fm1_guard.h)
 *   .data      initialized data and all read-only constants/jump tables, copied before C startup
 *   .bss       zeroed by C startup
 *   .pool      big zero-initialised buffers (FX lines, canvases), section .pool
 *   free       _pool_end .. _sram_end: what nothing is linked into (an app's own use, past the spare)
 * crt0 (felucca-bsp) saves the boot probe's words at 0x01C07F00.. before the start-up copies and zeroes the region:
 * they are written over there (nothing in fm1-base reads them).
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * CACHE 0x01F28000..0x01F2F000: data-cache ways used as RAM (fm1.toml cache_ram_ways, felucca-bsp's fm1_cache_ram.h).
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table.
 * The update loader runs at 0x01C0A800 after a reset into the update: it overwrites that part of SRAM, never while
 * the app runs. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x9DFBC   /* the app slot (0x8DFBC) and 64 KB: the image before
                                                       * boot_pack packs it; build.py checks the slot */
    SRAM   (rwx) : ORIGIN = 0x01C00000, LENGTH = 0x74000    /* up to the stacks' first guard band (_guard0) */
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
    CACHE  (rw)  : ORIGIN = 0x01F28000, LENGTH = 0x7000     /* data-cache ways 0..6 as RAM: only with fm1.toml cache_ram_ways
                                                           * and its test passed (plat_cache_ram); the cache's otherwise */
}
SECTIONS
{
    /* Only the initial entry/copy stub executes from flash. All runtime code,
     * jump tables and read-only constants live in SRAM, including IRQ wrappers.
     * CPU2's loop and jobs are SRAM resident too; vendor radio/packed boot stay excluded. */
    .text : { KEEP(*(.text.crt0 .text.crt0.*)) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*))
                  . = ALIGN(4); _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .;
                  _ar_start = .; *(.text*) *(.app_ram .app_ram.*)
                  . = ALIGN(4); _ar_end = .; _rt_end = .; } > SRAM AT > XIP
    _rt_load = LOADADDR(.ram_text);
    _app_xip_end = ORIGIN(XIP) + 0x8DFBC;            /* the app slot's end (the boot's bound on the packed load
                                                      * images: main_fm1.c) */
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) *(.rodata*) *(.*_data) . = ALIGN(4); _data_end = .; } > SRAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) *(.*_bss) *(.bd_base) . = ALIGN(4); _bss_end = .; } > SRAM
    .pool (NOLOAD) : { . = ALIGN(4); _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > SRAM
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    /* the cache RAM's objects (plat.h plat_cache_ram): zeroed by its test at the start, not by the boot; for an
     * object whose every user checks plat_cache_ram() first (without the test passed it is not RAM). Empty in
     * fm1-base itself; tools/build.py checks it fits the ways asked for (none: empty) */
    .cache_ram (NOLOAD) : { _cr_start = .; *(.cache_ram .cache_ram.*) . = ALIGN(4); _cr_end = .; } > CACHE
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    _sram_start = ORIGIN(SRAM); _sram_end = ORIGIN(SRAM) + LENGTH(SRAM);
    ASSERT(_sram_end == _guard0, "app_ram.ld: SRAM must end at the stacks' first guard band")
    /DISCARD/ : { *(.comment) *(.note*) }
}
