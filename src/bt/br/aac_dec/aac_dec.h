/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BT_AAC_DEC_H
#define BT_AAC_DEC_H
#include <stddef.h>
#include <stdint.h>

/* Single-owner task/worker API; never an ISR. One raw_data_block per call, never ADTS/RTP/LATM.
 * Compact 2-byte AudioSpecificConfig only: LC, 44.1/48 kHz, mono/stereo,
 * 1024-frame windows, no core dependency/extensions/PCE. No allocation occurs.
 * Workspace must have AAC_DEC_ALIGNMENT alignment and outlive the decoder.
 * PCM capacity is in int16 samples, frames counts samples per channel.
 * Workspace, AU, PCM and output metadata must not overlap.
 * All PCM becomes invalid on a failed decode; reset is automatic on bad data.
 * Separate instances may run concurrently; a single instance is not reentrant.
 */
#define AAC_DEC_ALIGNMENT 8u
#define AAC_DEC_MAX_AU_BYTES 2048u
#define AAC_DEC_PCM_SAMPLES 2048u

typedef struct aac_dec aac_dec;
typedef enum {
    AAC_DEC_OK = 0, AAC_DEC_BAD_ARGUMENT = -1, AAC_DEC_UNSUPPORTED = -2,
    AAC_DEC_NO_MEMORY = -3, AAC_DEC_BAD_FRAME = -4
} aac_dec_status;
typedef struct {
    uint32_t sample_rate;
    uint32_t frames;
    uint32_t consumed_bytes;
    uint8_t channels;
} aac_dec_output;

size_t aac_dec_workspace_size(void);
aac_dec_status aac_dec_init(void *workspace, size_t workspace_bytes,
                           const uint8_t *asc, size_t asc_bytes, aac_dec **decoder);
aac_dec_status aac_dec_decode(aac_dec *decoder, const uint8_t *au, size_t au_bytes,
                             int16_t *pcm, size_t pcm_samples, aac_dec_output *output);
aac_dec_status aac_dec_reset(aac_dec *decoder);
void aac_dec_destroy(aac_dec *decoder);
#endif
