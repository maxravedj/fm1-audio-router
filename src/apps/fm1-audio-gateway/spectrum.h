/* SPDX-License-Identifier: MPL-2.0
 * Forty logarithmic input-frequency bands. Foreground only: a periodic Hann
 * window and 4096-point radix-2 FFT (10.77 Hz/bin, 92.9 ms at 44.1 kHz).
 * Pack L and R into one complex FFT, then recover their powers separately so
 * opposite-polarity stereo never cancels. No changes to playback samples. */
#include "fastmath.h"
static uint16_t gateway_spectrum_color_value(void);
#define GATEWAY_SPECTRUM_BANDS 40u
#define GATEWAY_SPECTRUM_INTERVAL 33u
#define GATEWAY_SPECTRUM_HEIGHT 136u
#define GATEWAY_SPECTRUM_X 28u
#define GATEWAY_SPECTRUM_WIDTH 212u
#define GATEWAY_SPECTRUM_TOP 26u
#define GATEWAY_SPECTRUM_BOTTOM 162u
#define GATEWAY_SPECTRUM_FLOOR (-150000)
static const uint8_t gateway_spectrum_legacy_ranges[] = {15, 30, 60, 90};
static const uint8_t gateway_spectrum_rates[] = {48, 24, 12};
static const char *const gateway_spectrum_rate_names[] = {"FAST", "MID", "SLOW"};
static uint8_t gateway_spectrum_rate = 1, gateway_spectrum_max, gateway_spectrum_min = 90;

static int32_t gateway_spectrum_top_db(void) { return -(int32_t)gateway_spectrum_max * 1000; }
static int32_t gateway_spectrum_bottom_db(void)
{
    return -(int32_t)gateway_spectrum_min * 1000;
}
static uint32_t gateway_spectrum_height(int32_t db)
{
    int32_t bottom = gateway_spectrum_bottom_db();
    uint32_t span = 1000u * (gateway_spectrum_min-gateway_spectrum_max);
    if (db <= bottom) return 0;
    if (db >= bottom + (int32_t)span) return GATEWAY_SPECTRUM_HEIGHT;
    return ((uint32_t)(db - bottom) * GATEWAY_SPECTRUM_HEIGHT + span/2u) / span;
}
static uint32_t gateway_spectrum_x(uint32_t band)
{
    return GATEWAY_SPECTRUM_X + band * GATEWAY_SPECTRUM_WIDTH / GATEWAY_SPECTRUM_BANDS;
}
typedef struct {
    /* PCM is consumed before FFT butterflies. Expand backwards in-place so
     * unread int16 samples remain intact; this saves 16 KiB of display SRAM. */
    union {
        int16_t pcm[PLAT_SPECTRUM_FRAMES][2];
        float fft[PLAT_SPECTRUM_FRAMES][2];
    } work;
    /* Other half of the semicircle follows by reflection. */
    float twiddle[PLAT_SPECTRUM_FRAMES / 4u + 1u][2];
} gateway_spectrum_buffers_t;
static gateway_spectrum_buffers_t *gateway_spectrum_buffers;
#define gateway_spectrum_pcm (gateway_spectrum_buffers->work.pcm)
#define gateway_fft (gateway_spectrum_buffers->work.fft)
#define gateway_twiddle (gateway_spectrum_buffers->twiddle)
static uint8_t gateway_twiddle_ready, gateway_spectrum_have_position;
static float gateway_twiddle_part(unsigned phase,unsigned part)
{
    unsigned quarter=PLAT_SPECTRUM_FRAMES/4u;
    if(phase<=quarter)return gateway_twiddle[phase][part];
    float v=gateway_twiddle[PLAT_SPECTRUM_FRAMES/2u-phase][part];
    return part?v:-v;
}
static uint32_t gateway_spectrum_ms, gateway_spectrum_position;
#define GATEWAY_SPECTRUM_BAR_COLOR gateway_spectrum_color_value()
#define GATEWAY_SPECTRUM_PEAK_COLOR C_WHITE
#define GATEWAY_SPECTRUM_HOLD_MS 750u
/* Absolute milli-dBFS keeps ballistics independent of display zoom and offset. */
static int32_t gateway_spectrum_level[GATEWAY_SPECTRUM_BANDS];
static int32_t gateway_spectrum_peak[GATEWAY_SPECTRUM_BANDS];
static uint32_t gateway_spectrum_peak_at[GATEWAY_SPECTRUM_BANDS];
static uint16_t gateway_spectrum_peak_drawn[GATEWAY_SPECTRUM_BANDS];
static uint16_t gateway_spectrum_drawn[GATEWAY_SPECTRUM_BANDS];
static int32_t gateway_spectrum_target[GATEWAY_SPECTRUM_BANDS];
/* 20 * 1000^(band/40) * 4096/44100: fractional FFT-bin edges.
 * Integrate each bin's overlap with each band so narrow bass bands never
 * become empty integer-bin ranges. Their resolution remains 10.77 Hz. */
static const float gateway_spectrum_edges[GATEWAY_SPECTRUM_BANDS + 1u] = {
    1.85759637f, 2.20775743f, 2.62392462f, 3.11854025f, 3.70639204f,
    4.40505519f, 5.23541791f, 6.22230584f, 7.39522436f, 8.78924062f,
    10.44603205f, 12.41513236f, 14.75541247f, 17.53684059f, 20.84257410f,
    24.77144574f, 29.44091844f, 34.99059715f, 41.58640265f, 49.42553218f,
    58.74235508f, 69.81541986f, 82.97578202f, 98.61690175f, 117.20640739f,
    139.30007626f, 165.55845091f, 196.76658768f, 233.85752774f, 277.94019262f,
    330.33253803f, 392.60095724f, 466.60711218f, 554.56359216f, 659.10006454f,
    783.34189480f, 931.00358682f, 1106.49983669f, 1315.07752056f, 1562.97256244f,
    1857.59637188f
};

static void gateway_spectrum_reset(uint32_t now)
{
    uint32_t b;
    gateway_spectrum_ms = now - GATEWAY_SPECTRUM_INTERVAL;
    gateway_spectrum_have_position = 0;
    for (b = 0; b < GATEWAY_SPECTRUM_BANDS; b++) {
        gateway_spectrum_level[b] = gateway_spectrum_peak[b] = gateway_spectrum_target[b] = GATEWAY_SPECTRUM_FLOOR;
        gateway_spectrum_peak_at[b] = now;
        gateway_spectrum_drawn[b] = gateway_spectrum_peak_drawn[b] = 0;
    }
}

static void gateway_spectrum_analyze(void)
{
    uint32_t i, j, bit, len, start, b;
    int32_t sum_l = 0, sum_r = 0;
    float mean_l, mean_r;
    if (!gateway_twiddle_ready) {
        for (i = 0; i <= PLAT_SPECTRUM_FRAMES / 4u; i++) {
            if(!(i&31u))plat_bt_receiver_service();
            float angle = FM_TWO_PI * (float)i / (float)PLAT_SPECTRUM_FRAMES;
            gateway_twiddle[i][0] = fm_cosf(angle);
            gateway_twiddle[i][1] = -fm_sinf(angle);
        }
        gateway_twiddle_ready = 1;
    }
    for (i = 0; i < PLAT_SPECTRUM_FRAMES; i++) {
        if(!(i&127u))plat_bt_receiver_service();
        sum_l += gateway_spectrum_pcm[i][0];
        sum_r += gateway_spectrum_pcm[i][1];
    }
    mean_l = (float)sum_l / (float)PLAT_SPECTRUM_FRAMES;
    mean_r = (float)sum_r / (float)PLAT_SPECTRUM_FRAMES;
    for (i = PLAT_SPECTRUM_FRAMES; i-- > 0;) {
        if(!(i&127u))plat_bt_receiver_service();
        float c = gateway_twiddle_part(i & (PLAT_SPECTRUM_FRAMES/2u - 1u),0);
        float window = 0.5f - 0.5f * (i < PLAT_SPECTRUM_FRAMES/2u ? c : -c);
        float left=((float)gateway_spectrum_pcm[i][0]-mean_l)*window/32768.0f;
        float right=((float)gateway_spectrum_pcm[i][1]-mean_r)*window/32768.0f;
        gateway_fft[i][0]=left;gateway_fft[i][1]=right;
    }
    for (i = 1, j = 0; i < PLAT_SPECTRUM_FRAMES; i++) {
        if(!(i&127u))plat_bt_receiver_service();
        bit = PLAT_SPECTRUM_FRAMES / 2u;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) {
            float re = gateway_fft[i][0], im = gateway_fft[i][1];
            gateway_fft[i][0] = gateway_fft[j][0]; gateway_fft[i][1] = gateway_fft[j][1];
            gateway_fft[j][0] = re; gateway_fft[j][1] = im;
        }
    }
    for (len = 2; len <= PLAT_SPECTRUM_FRAMES; len *= 2u) {
        uint32_t half = len / 2u, stride = PLAT_SPECTRUM_FRAMES / len;
        for (start = 0; start < PLAT_SPECTRUM_FRAMES; start += len)
            for (j = 0; j < half; j++) {
                if(!((start+j)&127u))plat_bt_receiver_service();
                uint32_t a = start + j, z = a + half;
                float wr = gateway_twiddle_part(j * stride,0), wi = gateway_twiddle_part(j * stride,1);
                float re = wr * gateway_fft[z][0] - wi * gateway_fft[z][1];
                float im = wr * gateway_fft[z][1] + wi * gateway_fft[z][0];
                gateway_fft[z][0] = gateway_fft[a][0] - re;
                gateway_fft[z][1] = gateway_fft[a][1] - im;
                gateway_fft[a][0] += re; gateway_fft[a][1] += im;
            }
    }
    for (b = 0; b < GATEWAY_SPECTRUM_BANDS; b++) {
        plat_bt_receiver_service();
        float left = 0.0f, right = 0.0f, power, db;
        float lo = gateway_spectrum_edges[b], hi = gateway_spectrum_edges[b+1u];
        for (i = (uint32_t)(lo + 0.5f); (float)i - 0.5f < hi; i++) {
            uint32_t z = PLAT_SPECTRUM_FRAMES - i;
            float weight = fm_minf(hi, (float)i + 0.5f) - fm_maxf(lo, (float)i - 0.5f);
            float lr = 0.5f * (gateway_fft[i][0] + gateway_fft[z][0]);
            float li = 0.5f * (gateway_fft[i][1] - gateway_fft[z][1]);
            float rr = 0.5f * (gateway_fft[i][1] + gateway_fft[z][1]);
            float ri = 0.5f * (gateway_fft[z][0] - gateway_fft[i][0]);
            left += (lr * lr + li * li) * weight;
            right += (rr * rr + ri * ri) * weight;
        }
        /* Hann energy normalization: full-scale sine = 0 dB; take the louder
         * channel per band. Floor -150 dB; no log of zero/denormal values. */
        power = fm_maxf(left, right) * (32.0f / (3.0f * 4096.0f * 4096.0f));
        db = power > 1.0e-15f ? 3.0102999566f * fm_log2f(power) : -150.0f;
        db = fm_clampf(db, -150.0f, 0.0f);
        gateway_spectrum_target[b] = (int32_t)(db * 1000.0f - 0.5f);
    }
}

/* Foreground ballistics; elapsed-time arithmetic also survives the ms wrap.
 * A long foreground stall advances at most 250 ms, as in the previous view. */
static void gateway_spectrum_step(uint32_t now, uint32_t dt)
{
    uint32_t b;
    if (dt > 250u) dt = 250u;
    for (b = 0; b < GATEWAY_SPECTRUM_BANDS; b++) {
        int32_t target = gateway_spectrum_target[b];
        int32_t level = gateway_spectrum_level[b], peak = gateway_spectrum_peak[b];
        int32_t rate = gateway_spectrum_rates[gateway_spectrum_rate];
        uint32_t elapsed = now - gateway_spectrum_peak_at[b];
        level -= (int32_t)dt * rate;
        if (level < GATEWAY_SPECTRUM_FLOOR) level = GATEWAY_SPECTRUM_FLOOR;
        if (level < target) level = target;
        if (target > GATEWAY_SPECTRUM_FLOOR && target >= peak) {
            peak = target;
            gateway_spectrum_peak_at[b] = now;
        } else if (elapsed > GATEWAY_SPECTRUM_HOLD_MS) {
            uint32_t release = elapsed - GATEWAY_SPECTRUM_HOLD_MS;
            if (release > dt) release = dt;
            peak -= (int32_t)release * (rate / 2);
            if (peak < GATEWAY_SPECTRUM_FLOOR) peak = GATEWAY_SPECTRUM_FLOOR;
        }
        if (peak < level) peak = level;
        gateway_spectrum_level[b] = level;
        gateway_spectrum_peak[b] = peak;
    }
}

static void gateway_spectrum_number(char out[8], int32_t value)
{
    uint32_t n = 0, v;
    if (value < 0) { out[n++] = '-'; value = -value; }
    v = (uint32_t)value;
    if (v >= 100u) out[n++] = '0' + v/100u;
    if (v >= 10u) out[n++] = '0' + v/10u%10u;
    out[n++] = '0' + v%10u;
    out[n] = 0;
}

static void gateway_spectrum_paint(int force)
{
    uint32_t b;
    for (b = 0; b < GATEWAY_SPECTRUM_BANDS; b++) {
        uint32_t x = gateway_spectrum_x(b), width = gateway_spectrum_x(b+1u) - x - 1u;
        uint32_t old = gateway_spectrum_drawn[b], old_peak = gateway_spectrum_peak_drawn[b];
        uint32_t level = gateway_spectrum_height(gateway_spectrum_level[b]);
        uint32_t peak = gateway_spectrum_height(gateway_spectrum_peak[b]);
        if (force) {
            gateway_display_fill(x, GATEWAY_SPECTRUM_TOP, width, GATEWAY_SPECTRUM_HEIGHT, C_BLACK);
            old = old_peak = 0;
        }
        if (level > old) gateway_display_fill(x, GATEWAY_SPECTRUM_BOTTOM - level, width, level - old, GATEWAY_SPECTRUM_BAR_COLOR);
        else if (level < old) gateway_display_fill(x, GATEWAY_SPECTRUM_BOTTOM - old, width, old - level, C_BLACK);
        if (old_peak != peak || old != level || force) {
            /* Erase a moved peak, restoring the bar underneath when it grew
             * through that pixel. Then paint its new line above the bars. */
            if (old_peak) gateway_display_fill(x, GATEWAY_SPECTRUM_BOTTOM - old_peak, width, 1,
                old_peak <= level ? GATEWAY_SPECTRUM_BAR_COLOR : C_BLACK);
            if (peak) gateway_display_fill(x, GATEWAY_SPECTRUM_BOTTOM - peak, width, 1, GATEWAY_SPECTRUM_PEAK_COLOR);
        }
        gateway_spectrum_drawn[b] = (uint16_t)level;
        gateway_spectrum_peak_drawn[b] = (uint16_t)peak;
    }
    if (force) {
        char text[8];
        uint32_t tick;
        int32_t top = -(int32_t)gateway_spectrum_max;
        uint32_t span = gateway_spectrum_min-gateway_spectrum_max,steps=span<6u?span:6u;
        /* Tight right-aligned dB ticks. This canvas is left of the bars. */
        for(unsigned band=0;band<154;band+=APP_CANVAS_PX/GATEWAY_SPECTRUM_X){
        unsigned h=154-band;if(h>APP_CANVAS_PX/GATEWAY_SPECTRUM_X)h=APP_CANVAS_PX/GATEWAY_SPECTRUM_X;
        cv_begin(GATEWAY_SPECTRUM_X,h,C_BLACK);cv_oy=-(int)band;
        for (tick = 0; tick <= steps; tick++) {
            int32_t db = top - (int32_t)(tick * span / steps);
            int32_t y = 6 + (int32_t)(tick * GATEWAY_SPECTRUM_HEIGHT / steps) - FONT_T.h/2;
            gateway_spectrum_number(text, db);
            cv_text(24 - text_w(&FONT_T, text), y, &FONT_T, text, C_GRAY);
        }
        cv_blit(0,20+band);
        }
        /* Numeric frequency labels in a tight strip at the bottom edge. */
        cv_begin(GATEWAY_SPECTRUM_WIDTH, 12, C_BLACK);
        cv_text(0, 0, &FONT_T, "20", RGB(190, 170, 60));
        cv_text(44, 0, &FONT_T, "100", RGB(190, 170, 60));
        cv_text(115, 0, &FONT_T, "1k", RGB(190, 170, 60));
        cv_text(179, 0, &FONT_T, "10k", RGB(190, 170, 60));
        cv_text(197, 0, &FONT_T, "20k", RGB(190, 170, 60));
        cv_blit(GATEWAY_SPECTRUM_X, 166);
    }
}

static void gateway_spectrum_draw(uint32_t now, int force)
{
    uint32_t b, position = 0, dt = now - gateway_spectrum_ms;
    plat_profile_span_t timing=plat_profile_begin();
    int valid = plat_usb_input_spectrum(gateway_spectrum_pcm, &position);
    plat_profile_end(PROFILE_SPEC_READ,timing);
    if (valid && (!gateway_spectrum_have_position || position != gateway_spectrum_position)) {
        timing=plat_profile_begin();gateway_spectrum_analyze();plat_profile_end(PROFILE_FFT,timing);
        gateway_spectrum_have_position = 1;
        gateway_spectrum_position = position;
    } else if (!valid) {
        gateway_spectrum_have_position = 0;
        for (b = 0; b < GATEWAY_SPECTRUM_BANDS; b++) gateway_spectrum_target[b] = GATEWAY_SPECTRUM_FLOOR;
    }
    gateway_spectrum_ms = now;
    gateway_spectrum_step(now, dt);
    timing=plat_profile_begin();gateway_spectrum_paint(force);plat_profile_end(PROFILE_SPEC_DRAW,timing);
}
