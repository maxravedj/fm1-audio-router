/* SPDX-License-Identifier: Apache-2.0 */
#include "aac_dec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned rng = 0x194aacu;
static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static unsigned read32(FILE *f) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return b[0] | ((unsigned)b[1] << 8) | ((unsigned)b[2] << 16) | ((unsigned)b[3] << 24);
}
int main(int argc, char **argv) {
    size_t memsize = aac_dec_workspace_size();
    void *mem = malloc(memsize);
    aac_dec *d = 0;
    uint8_t asc[2], *au;
    int16_t pcm[AAC_DEC_PCM_SAMPLES];
    aac_dec_output out;
    FILE *in, *output;
    unsigned n, good = 0, rounds = argc > 3 ? (unsigned)atoi(argv[3]) : 0;
    assert(argc >= 3 && mem);
    in = fopen(argv[1], "rb"); output = fopen(argv[2], "wb"); assert(in && output);
    assert(fread(asc, 1, 2, in) == 2);
    assert(aac_dec_init(mem, memsize, asc, 2, &d) == AAC_DEC_OK);
    assert(aac_dec_decode(d, asc, 2, pcm, AAC_DEC_PCM_SAMPLES - 1, &out) == AAC_DEC_NO_MEMORY);
    assert(aac_dec_decode(d, asc, 0, pcm, AAC_DEC_PCM_SAMPLES, &out) == AAC_DEC_BAD_FRAME);
    assert(aac_dec_decode(d, asc, AAC_DEC_MAX_AU_BYTES + 1, pcm, AAC_DEC_PCM_SAMPLES, &out) == AAC_DEC_BAD_FRAME);
    assert(aac_dec_reset(NULL) == AAC_DEC_BAD_ARGUMENT);
    aac_dec_destroy(NULL);
    assert(aac_dec_decode(NULL, asc, 2, pcm, AAC_DEC_PCM_SAMPLES, &out) == AAC_DEC_BAD_ARGUMENT);
    assert(aac_dec_decode(d, NULL, 2, pcm, AAC_DEC_PCM_SAMPLES, &out) == AAC_DEC_BAD_ARGUMENT);
    assert(aac_dec_decode(d, asc, 2, NULL, AAC_DEC_PCM_SAMPLES, &out) == AAC_DEC_BAD_ARGUMENT);
    assert(aac_dec_decode(d, asc, 2, pcm, AAC_DEC_PCM_SAMPLES, NULL) == AAC_DEC_BAD_ARGUMENT);
    assert(aac_dec_decode(d, (const uint8_t *)mem, 2, pcm, AAC_DEC_PCM_SAMPLES, &out) == AAC_DEC_BAD_ARGUMENT);
    assert(aac_dec_decode(d, (const uint8_t *)pcm, 2, pcm, AAC_DEC_PCM_SAMPLES, &out) == AAC_DEC_BAD_ARGUMENT);
    assert(aac_dec_decode(d, asc, 2, pcm, AAC_DEC_PCM_SAMPLES, (aac_dec_output *)pcm) == AAC_DEC_BAD_ARGUMENT);
    while ((n = read32(in))) {
        assert(n <= AAC_DEC_MAX_AU_BYTES);
        au = malloc(n); assert(au && fread(au, 1, n, in) == n);
        if (aac_dec_decode(d, au, n, pcm, AAC_DEC_PCM_SAMPLES, &out) != AAC_DEC_OK) {
            fprintf(stderr, "valid frame %u failed (%u bytes)\n", good, n); abort();
        }
        assert(out.frames == 1024 && out.consumed_bytes == n);
        assert(out.channels == ((asc[1] >> 3) & 15));
        assert(out.sample_rate == ((asc[0] & 7) == 1 ? 48000u : 44100u));
        assert(fwrite(pcm, sizeof *pcm, out.frames * out.channels, output) == out.frames * out.channels);
        good++;
        if (rounds) {
            for (unsigned j = 1; j < n; j += 1u + n / 64u) {
                uint8_t *b = malloc(j); assert(b); memcpy(b, au, j);
                assert(aac_dec_reset(d) == AAC_DEC_OK);
                assert(aac_dec_decode(d, b, j, pcm, AAC_DEC_PCM_SAMPLES, &out) == AAC_DEC_BAD_FRAME); free(b);
            }
            for (unsigned j = 0; j < rounds; j++) {
                unsigned len = (j & 1u) ? n : 1u + rnd() % AAC_DEC_MAX_AU_BYTES;
                uint8_t *b = malloc(len); assert(b);
                for (unsigned k = 0; k < len; k++) b[k] = (uint8_t)rnd();
                if (j & 1u) { memcpy(b, au, len); for (unsigned k=0;k<1u+j%8u;k++) b[rnd()%len] ^= (uint8_t)(1u << (rnd()&7)); }
                assert(aac_dec_reset(d) == AAC_DEC_OK);
                aac_dec_status status = aac_dec_decode(d, b, len, pcm, AAC_DEC_PCM_SAMPLES, &out);
                assert(status == AAC_DEC_OK || status == AAC_DEC_BAD_FRAME); free(b);
            }
            int16_t again[AAC_DEC_PCM_SAMPLES]; aac_dec_output again_out;
            assert(aac_dec_reset(d) == AAC_DEC_OK);
            assert(aac_dec_decode(d, au, n, pcm, AAC_DEC_PCM_SAMPLES, &out) == AAC_DEC_OK);
            assert(aac_dec_reset(d) == AAC_DEC_OK);
            assert(aac_dec_decode(d, au, n, again, AAC_DEC_PCM_SAMPLES, &again_out) == AAC_DEC_OK);
            assert(!memcmp(pcm, again, out.frames * out.channels * sizeof *pcm));
        }
        free(au);
    }
    aac_dec_destroy(d);
    assert(aac_dec_reset(d) == AAC_DEC_BAD_ARGUMENT);
    for (unsigned bits = 0; bits < 65536; bits++) {
        uint8_t c[2] = { (uint8_t)(bits >> 8), (uint8_t)bits };
        unsigned rate = ((c[0] & 7u) << 1) | (c[1] >> 7), ch = (c[1] >> 3) & 15u;
        int valid = (c[0] >> 3) == 2u && (rate == 3u || rate == 4u) && (ch == 1u || ch == 2u) && !(c[1] & 7u);
        assert(aac_dec_init(mem, memsize, c, 2, &d) == (valid ? AAC_DEC_OK : AAC_DEC_UNSUPPORTED)); aac_dec_destroy(d);
    }
    assert(aac_dec_init(mem, memsize, asc, 1, &d) == AAC_DEC_UNSUPPORTED);
    assert(aac_dec_init(mem, memsize-1, asc, 2, &d) == AAC_DEC_NO_MEMORY);
    assert(aac_dec_init((uint8_t *)mem+1, memsize, asc, 2, &d) == AAC_DEC_BAD_ARGUMENT);
    fclose(in); fclose(output); free(mem);
    fprintf(stderr, "PASS %u raw AUs; workspace=%zu; fuzz_per_AU=%u\n", good, memsize, rounds);
    return 0;
}
