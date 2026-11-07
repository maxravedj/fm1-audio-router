/* SPDX-License-Identifier: Apache-2.0 */
#include "aac_dec.h"
#include <string.h>
#include "pvaac/pvmp4audiodecoder_api.h"

#define AAC_MAGIC 0x41414331u
struct aac_dec {
    tPVMP4AudioDecoderExternal ext;
    uint32_t magic;
    uint32_t sample_rate;
    uint8_t asc[2];
    uint8_t channels;
};
static size_t header_size(void)
{
    return (sizeof(aac_dec) + AAC_DEC_ALIGNMENT - 1u) & ~(size_t)(AAC_DEC_ALIGNMENT - 1u);
}
/* Compare by subtraction so invalid caller lengths cannot wrap an endpoint. */
static int overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < an : x - y < bn;
}
static void *state(aac_dec *d) { return (uint8_t *)d + header_size(); }
size_t aac_dec_workspace_size(void)
{
    return header_size() + PVMP4AudioDecoderGetMemRequirements();
}
aac_dec_status aac_dec_reset(aac_dec *d)
{
    if (!d || d->magic != AAC_MAGIC) return AAC_DEC_BAD_ARGUMENT;
    memset(&d->ext, 0, sizeof d->ext);
    d->ext.outputFormat = OUTPUTFORMAT_16PCM_INTERLEAVED;
    d->ext.desiredChannels = d->channels;
    if (PVMP4AudioDecoderInitLibrary(&d->ext, state(d)) != MP4AUDEC_SUCCESS)
        return AAC_DEC_BAD_FRAME;
    d->ext.aacPlusEnabled = false;
    d->ext.pInputBuffer = d->asc;
    d->ext.inputBufferCurrentLength = sizeof d->asc;
    if (PVMP4AudioDecoderConfig(&d->ext, state(d)) != MP4AUDEC_SUCCESS ||
        d->ext.audioObjectType != 2 || d->ext.encodedChannels != d->channels ||
        d->ext.samplingRate != (Int32)d->sample_rate)
        return AAC_DEC_UNSUPPORTED;
    return AAC_DEC_OK;
}
aac_dec_status aac_dec_init(void *memory, size_t bytes,
                           const uint8_t *asc, size_t asc_bytes, aac_dec **result)
{
    unsigned object, rate, channels;
    uint8_t config[2];
    aac_dec *d;
    if (!result) return AAC_DEC_BAD_ARGUMENT;
    *result = 0;
    if (!memory || !asc || ((uintptr_t)memory & (AAC_DEC_ALIGNMENT - 1u)))
        return AAC_DEC_BAD_ARGUMENT;
    if (bytes < aac_dec_workspace_size()) return AAC_DEC_NO_MEMORY;
    if (asc_bytes != 2u) return AAC_DEC_UNSUPPORTED;
    object = asc[0] >> 3;
    rate = ((asc[0] & 7u) << 1) | (asc[1] >> 7);
    channels = (asc[1] >> 3) & 15u;
    if (object != 2u || (rate != 3u && rate != 4u) ||
        (channels != 1u && channels != 2u) || (asc[1] & 7u))
        return AAC_DEC_UNSUPPORTED;
    memcpy(config, asc, sizeof config);
    d = memory;
    memset(d, 0, header_size());
    d->magic = AAC_MAGIC;
    d->sample_rate = rate == 3u ? 48000u : 44100u;
    d->channels = (uint8_t)channels;
    memcpy(d->asc, config, sizeof d->asc);
    if (aac_dec_reset(d) != AAC_DEC_OK) {
        aac_dec_destroy(d);
        return AAC_DEC_UNSUPPORTED;
    }
    *result = d;
    return AAC_DEC_OK;
}
aac_dec_status aac_dec_decode(aac_dec *d, const uint8_t *au, size_t bytes,
                             int16_t *pcm, size_t capacity, aac_dec_output *out)
{
    int status;
    if (!d || d->magic != AAC_MAGIC || !out || !au || !pcm)
        return AAC_DEC_BAD_ARGUMENT;
    if (capacity < AAC_DEC_PCM_SAMPLES) return AAC_DEC_NO_MEMORY;
    if (!bytes || bytes > AAC_DEC_MAX_AU_BYTES) {
        aac_dec_reset(d);
        return AAC_DEC_BAD_FRAME;
    }
    if (((uintptr_t)pcm & (sizeof *pcm - 1u)) ||
        overlaps(d, aac_dec_workspace_size(), au, bytes) ||
        overlaps(d, aac_dec_workspace_size(), pcm, AAC_DEC_PCM_SAMPLES * sizeof *pcm) ||
        overlaps(d, aac_dec_workspace_size(), out, sizeof *out) ||
        overlaps(au, bytes, pcm, AAC_DEC_PCM_SAMPLES * sizeof *pcm) ||
        overlaps(out, sizeof *out, pcm, AAC_DEC_PCM_SAMPLES * sizeof *pcm) ||
        overlaps(out, sizeof *out, au, bytes)) return AAC_DEC_BAD_ARGUMENT;
    memset(out, 0, sizeof *out);
    d->ext.pInputBuffer = (uint8_t *)au;
    d->ext.inputBufferCurrentLength = (int)bytes;
    d->ext.inputBufferUsedLength = 0;
    d->ext.remainderBits = 0;
    d->ext.pOutputBuffer = pcm;
    status = PVMP4AudioDecodeFrame(&d->ext, state(d));
    if (status != MP4AUDEC_SUCCESS || d->ext.frameLength <= 0 ||
        d->ext.frameLength > 1024 || d->ext.encodedChannels != d->channels ||
        d->ext.samplingRate != (Int32)d->sample_rate ||
        d->ext.inputBufferUsedLength != (int)bytes || d->ext.audioObjectType != 2) {
        memset(pcm, 0, AAC_DEC_PCM_SAMPLES * sizeof *pcm);
        aac_dec_reset(d);
        return AAC_DEC_BAD_FRAME;
    }
    out->frames = (uint32_t)d->ext.frameLength;
    out->channels = d->channels;
    out->sample_rate = d->sample_rate;
    out->consumed_bytes = (uint32_t)d->ext.inputBufferUsedLength;
    return AAC_DEC_OK;
}
void aac_dec_destroy(aac_dec *d)
{
    if (d && d->magic == AAC_MAGIC) memset(d, 0, aac_dec_workspace_size());
}
