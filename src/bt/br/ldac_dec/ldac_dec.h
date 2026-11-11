/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Fixed-point decoder for the CPU1 Bluetooth worker.
 * One exclusive owner; no IRQ calls. Instance storage belongs to the caller. */
typedef struct fm1_ldac_dec fm1_ldac_dec;
enum { FM1_LDAC_ALIGNMENT = 8, FM1_LDAC_SAMPLES = 128, FM1_LDAC_MAX_FRAME = 515,
       FM1_LDAC_ARGUMENT = -1, FM1_LDAC_FRAME = -2, FM1_LDAC_DATA = -3 };
size_t fm1_ldac_dec_size(void);
/* Length of a complete supported frame at the start of input, or zero.
 * Stateless/header-only; safe for the CPU0 packet validator. */
size_t fm1_ldac_frame_size(const uint8_t *frame,size_t bytes,uint32_t rate);
fm1_ldac_dec *fm1_ldac_dec_init(void *memory, size_t bytes, uint32_t rate);
int fm1_ldac_dec_reset(fm1_ldac_dec *decoder);
void fm1_ldac_dec_destroy(fm1_ldac_dec *decoder);
/* Exactly one complete LDAC frame, including its three-byte sync header.
 * Stereo 44.1/48 kHz only. Returns 128 frames or a negative error.
 * Output is interleaved signed 24-bit PCM in int32 slots (no gain/resampling).
 * Capacity is in stereo frames. Discard output on error. Data/frame errors
 * clear filterbank history; argument errors leave state/output untouched.
 * Memory, input and output must be disjoint, with valid lifetimes. */
int fm1_ldac_dec_frame(fm1_ldac_dec *decoder, const uint8_t *frame,
                       size_t bytes, int32_t (*pcm)[2], size_t capacity);
/* Exclusive decoder owner only. Decode two known frames into the caller's
 * ordinary output scratch, then reset history. No allocation/publication.
 * Returns bit 0/1 for a matching left/right checksum (3 = both pass).
 * hash receives the actual per-channel checksums for diagnostic logs. */
unsigned fm1_ldac_dec_check(fm1_ldac_dec *decoder,int32_t (*pcm)[2],uint32_t hash[2]);
