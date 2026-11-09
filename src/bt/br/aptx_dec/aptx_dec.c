/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* FM-1 decoder build: SRAM scratch is owned by CPU0, never libc heap. */
#define FM1_APTX_NO_HEAP
#include "freeaptx.c"
