/* SPDX-License-Identifier: MPL-2.0
 * Stereo FFT energy, level scaling, capture gating and stream invalidation.
 * Includes an analytic bin-centered Hann-energy oracle for all forty bands. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <math.h>
static int32_t pcm[2u * PLAT_SPECTRUM_FRAMES];
static int16_t snapshot[PLAT_SPECTRUM_FRAMES][2];
static float reference_fft[PLAT_SPECTRUM_FRAMES][2];
static float reference_twiddle[PLAT_SPECTRUM_FRAMES / 2u][2];
/* Group-first scalar reference for the phase-first FFT optimization. Build
 * bit-reversed input out-of-place, so this also checks the overlapping PCM
 * expansion. Keep the original arithmetic order, including signed zero. The
 * analytic tests below independently check window/band normalization. */
static void reference_transform(void)
{
    int32_t sums[2] = {0, 0};
    for (unsigned i = 0; i < PLAT_SPECTRUM_FRAMES; i++)
        for (unsigned ch = 0; ch < 2; ch++) sums[ch] += snapshot[i][ch];
    for (unsigned i = 0; i < PLAT_SPECTRUM_FRAMES / 2u; i++)
        for (unsigned ch = 0; ch < 2; ch++)
            reference_twiddle[i][ch] = gateway_twiddle_part(i, ch);
    for (unsigned i = 0; i < PLAT_SPECTRUM_FRAMES; i++) {
        unsigned reversed = 0;
        for (unsigned bit = PLAT_SPECTRUM_FRAMES / 2u, v = i; bit; bit >>= 1, v >>= 1)
            if (v & 1u) reversed |= bit;
        float c = reference_twiddle[i & (PLAT_SPECTRUM_FRAMES / 2u - 1u)][0];
        float window = 0.5f - 0.5f * (i < PLAT_SPECTRUM_FRAMES / 2u ? c : -c);
        for (unsigned ch = 0; ch < 2; ch++) {
            float mean = (float)sums[ch] / (float)PLAT_SPECTRUM_FRAMES;
            reference_fft[reversed][ch] = ((float)snapshot[i][ch] - mean) * window / 32768.0f;
        }
    }
    for (unsigned len = 2; len <= PLAT_SPECTRUM_FRAMES; len *= 2u) {
        unsigned half = len / 2u, stride = PLAT_SPECTRUM_FRAMES / len;
        for (unsigned start = 0; start < PLAT_SPECTRUM_FRAMES; start += len)
            for (unsigned j = 0; j < half; j++) {
                unsigned a = start + j, z = a + half;
                float wr = reference_twiddle[j * stride][0], wi = reference_twiddle[j * stride][1];
                float re = wr * reference_fft[z][0] - wi * reference_fft[z][1];
                float im = wr * reference_fft[z][1] + wi * reference_fft[z][0];
                reference_fft[z][0] = reference_fft[a][0] - re;
                reference_fft[z][1] = reference_fft[a][1] - im;
                reference_fft[a][0] += re; reference_fft[a][1] += im;
            }
    }
}
static void fft_equivalence(void)
{
    uint32_t random = 731;
    for (unsigned fixture = 0; fixture < 20; fixture++) {
        for (unsigned i = 0; i < PLAT_SPECTRUM_FRAMES; i++) {
            int16_t left = 0, right = 0;
            random = random * 1664525u + 1013904223u;
            if (fixture == 1) { left = 32767; right = -32768; } /* DC */
            else if (fixture == 2) { left = i == 1 ? -32768 : 0; right = i == 2048 ? 32767 : 0; }
            else if (fixture == 3) { left = i & 1u ? 32767 : -32768; right = -left / 2; }
            else if (fixture == 4) { left = (int16_t)(random >> 16) % 3; right = -left; }
            else if (fixture >= 5) {
                left = (int16_t)(random >> 16);
                random = random * 1664525u + 1013904223u;
                right = fixture == 5 ? left : fixture == 6 ? -(left / 2) : (int16_t)(random >> 16);
            }
            snapshot[i][0] = left; snapshot[i][1] = right;
        }
        memcpy(gateway_spectrum_pcm, snapshot, sizeof snapshot);
        if (fixture < 2) gateway_twiddle_ready = 0; /* cold and warm lookups */
        gateway_spectrum_analyze();
        reference_transform();
        assert(!memcmp(gateway_fft, reference_fft, sizeof reference_fft));
    }
    puts("PASS: all 4096 complex FFT bins bit-identical to group-first reference on 20 stereo fixtures, cold/warm table and in-place PCM expansion");
}
static unsigned loudest(void)
{
    unsigned i, best = 0;
    for (i = 1; i < GATEWAY_SPECTRUM_BANDS; i++)
        if (gateway_spectrum_target[i] > gateway_spectrum_target[best]) best = i;
    return best;
}
static void tones(float left, float right, float amplitude, int inverted)
{
    unsigned i;
    for (i = 0; i < PLAT_SPECTRUM_FRAMES; i++) {
        double t = 6.2831853071795864769 * (double)i / 44100.0;
        gateway_spectrum_pcm[i][0] = (int16_t)(32767.0 * amplitude * sin(t * left));
        gateway_spectrum_pcm[i][1] = (int16_t)(32767.0 * amplitude * sin(t * right) * (inverted ? -1.0 : 1.0));
    }
    gateway_spectrum_analyze();
}
/* A bin-centered sinusoid through a periodic Hann has exactly three nonzero
 * positive-frequency bins: energy fractions 1/6, 2/3, 1/6. This reference
 * checks the complete FFT + stereo recovery + fractional-band integration,
 * independently of the production FFT and precomputed edge table. */
static void analytic_bands(void)
{
    unsigned b, tone, k;
    const double amplitude = 0.4 * 32767.0 / 32768.0;
    assert(GATEWAY_SPECTRUM_BANDS == 40);
    for (tone = 0; tone < 40; tone++) {
        double center = 20.0 * pow(1000.0, ((double)tone + 0.5) / 40.0);
        unsigned bin = (unsigned)(center * 4096.0 / 44100.0 + 0.5);
        tones((float)bin * (44100.0f / 4096.0f), 0.0f, 0.4f, 0);
        assert(gateway_spectrum_target[tone] > -72000); /* no empty bass bands */
        for (b = 0; b < 40; b++) {
            double lo = 20.0 * pow(1000.0, (double)b / 40.0) * 4096.0 / 44100.0;
            double hi = 20.0 * pow(1000.0, (double)(b+1) / 40.0) * 4096.0 / 44100.0;
            double power = 0.0;
            for (k = bin-1; k <= bin+1; k++) {
                double overlap = fmin(hi, (double)k+0.5) - fmax(lo, (double)k-0.5);
                if (overlap > 0) power += overlap * (k == bin ? 2.0/3.0 : 1.0/6.0);
            }
            power *= amplitude * amplitude;
            double db = power > 0.0 ? 10.0*log10(power) : -150.0;
            /* Quantized PCM and float FFT roundoff contribute real low-level
             * energy outside the ideal three bins. Check the useful signal
             * to 0.15 dB; out-of-band residual must stay below -72 dBFS. */
            if (db > -72.0) {
                assert(fabs((double)gateway_spectrum_target[b]/1000.0-db) < 0.15);
            } else assert(gateway_spectrum_target[b] < -72000);

        }
    }
    /* Off-bin boundary tones: energy crosses adjacent bands without a hole. */
    for (b = 12; b < 39; b++) {
        float hz = (float)(20.0 * pow(1000.0, (double)b / 40.0));
        tones(hz, 0.0f, 0.5f, 0);
        assert(gateway_spectrum_target[b-1] > -15000 && gateway_spectrum_target[b] > -15000);
    }
    tones(0.0f, 1000.0f, 0.5f, 0);
    assert(loudest() == 22); /* right-only tone uses the same band mapping */
    puts("PASS: 40-band analytical Hann-energy oracle (1600 comparisons), boundary tones and right-only input");
}
int main(void)
{
    unsigned i, count;
    int32_t full, opposite;
    uint32_t position;
    app_init();
    gateway_mode=GATEWAY_MODE_SPECTRUM;
    gateway_view_changed();
    fft_equivalence();
    analytic_bands();
    tones(440.0f, 660.0f, 0.5f, 0);
    assert(gateway_spectrum_target[17] > -10000 && gateway_spectrum_target[20] > -10000);
    tones(1000.0f, 0.0f, 0.5f, 0);
    assert(loudest() == 22);
    full = gateway_spectrum_target[22];
    tones(1000.0f, 1000.0f, 0.5f, 1);
    opposite = gateway_spectrum_target[22];
    assert(abs((int)full - (int)opposite) <= 50); /* stereo anti-phase cannot cancel */
    tones(1000.0f, 0.0f, 0.05f, 0);
    assert(abs((int)full - (int)gateway_spectrum_target[22] - 20000) <= 150);
    for (i = 0; i < PLAT_SPECTRUM_FRAMES; i++) {
        gateway_spectrum_pcm[i][0] = 32767;
        gateway_spectrum_pcm[i][1] = -32768;
    }
    gateway_spectrum_analyze();
    for (i = 0; i < GATEWAY_SPECTRUM_BANDS; i++) assert(gateway_spectrum_target[i] == GATEWAY_SPECTRUM_FLOOR);
    tones(0.0f, 0.0f, 0.0f, 0);
    for (i = 0; i < GATEWAY_SPECTRUM_BANDS; i++) assert(gateway_spectrum_target[i] == GATEWAY_SPECTRUM_FLOOR);
    plat_usb_input_observe(0, 0);
    plat_usb_spectrum_enable(1);
    now_ms = 1000;
    for (i = 0; i < PLAT_SPECTRUM_FRAMES; i++) {
        pcm[2*i] = ((int32_t)i - 2048) * 256;
        pcm[2*i+1] = -pcm[2*i];
    }
    usb_input_meter_samples(pcm, PLAT_SPECTRUM_FRAMES);
    assert(plat_usb_input_spectrum(snapshot, &position));
    for (i = 0; i < PLAT_SPECTRUM_FRAMES; i++)
        assert(snapshot[i][0] == (pcm[2*i] >> 8) && snapshot[i][1] == (pcm[2*i+1] >> 8));
    count = usb_spectrum_count;
    plat_usb_spectrum_enable(0);
    usb_input_meter_samples(pcm, PLAT_SPECTRUM_FRAMES);
    assert(usb_spectrum_count == count && !plat_usb_input_spectrum(snapshot, &position));
    plat_usb_spectrum_enable(1);
    usb_input_meter_samples(pcm, PLAT_SPECTRUM_FRAMES);
    now_ms += 100;
    assert(!plat_usb_input_spectrum(snapshot, &position));
    memset(pcm, 0, sizeof pcm);
    usb_input_meter_samples(pcm, 1);
    assert(!plat_usb_input_spectrum(snapshot, &position));
    usb_input_meter_samples(pcm, PLAT_SPECTRUM_FRAMES - 1u);
    assert(plat_usb_input_spectrum(snapshot, &position));
    for (i = 0; i < PLAT_SPECTRUM_FRAMES; i++) assert(!snapshot[i][0] && !snapshot[i][1]);
    usb_input_stream_start();
    assert(!plat_usb_input_spectrum(snapshot, &position));
    usb_spectrum_count = UINT32_MAX - 2047u;
    usb_input_meter_samples(pcm, PLAT_SPECTRUM_FRAMES);
    assert(plat_usb_input_spectrum(snapshot, &position) && position == 2048u);
    assert(!cpu_work_depth);
    puts("PASS: spectral bands/stereo polarity/levels/DC/silence, hidden capture, stale/restart and frame-counter wrap");
}
