/* SPDX-License-Identifier: MPL-2.0
 * Input sample-peak display. dB conversion and ballistics run on the main thread. */
typedef struct {
    uint32_t level, held, hold_ms, clip_ms, release_rem, peak_rem;
    uint32_t peak; /* Numeric hold: tenths above -150 dBFS; 0 = silence. */
} gateway_meter_t;
#define GATEWAY_PEAK_RANGE 1500u
#define GATEWAY_PEAK_OFFSET (GATEWAY_PEAK_RANGE-600u)

/* 24-bit magnitudes at -60..0 dBFS, one dB per entry: round(2^23 * 10^(dB/20)). */
static const uint32_t gateway_db_threshold[61] = {
    8389, 9412, 10561, 11849, 13295, 14917, 16737, 18780, 21071, 23642, 26527, 29764, 33396, 37471, 42043, 47173, 52929, 59387, 66633, 74764, 83886, 94122, 105606, 118492, 132950, 149173, 167375, 187798, 210712, 236423, 265271, 297639, 333956, 374705, 420426, 471726, 529285, 593868, 666331, 747635, 838861, 941217, 1056063, 1184922, 1329505, 1491729, 1673747, 1877975, 2107123, 2364231, 2652711, 2976390, 3339565, 3747054, 4204263, 4717261, 5292854, 5938680, 6663308, 7476355, 8388608
};

/* 0..600 tenths of a dB above the -60 dBFS floor. */
static uint32_t gateway_meter_level(uint32_t peak)
{
    uint32_t i = 0;
    if (peak >= 8388352u) return 600u; /* either signed 16-bit full-scale endpoint */
    if (peak < gateway_db_threshold[0]) return 0;
    while (i < 60u && peak >= gateway_db_threshold[i + 1u]) i++;
    return i * 10u + (peak - gateway_db_threshold[i]) * 10u /
           (gateway_db_threshold[i + 1u] - gateway_db_threshold[i]);
}

/* Reuse the existing one-dB interpolation below the bars' -60 dB floor.
 * Scaling by ten adds exactly 20 dB; even a one-LSB 24-bit peak is retained.
 * The largest scaled value is below 83890, so multiplication cannot overflow. */
static uint32_t gateway_meter_peak_level(uint32_t peak)
{
    if(!peak)return 0;
    uint32_t offset=GATEWAY_PEAK_OFFSET;
    while(peak<gateway_db_threshold[0]){peak*=10u;offset-=200u;}
    return gateway_meter_level(peak)+offset;
}

static int gateway_meter_step(gateway_meter_t *m, uint32_t peak, uint32_t dt)
{
    uint32_t numeric=gateway_meter_peak_level(peak),previous_peak=m->peak;
    uint32_t input = numeric>GATEWAY_PEAK_OFFSET?numeric-GATEWAY_PEAK_OFFSET:0;
    uint32_t level = m->level, held = m->held;
    uint32_t clipped = m->clip_ms != 0, fall;
    /* Immediate attack, 18 dB/s release; 750 ms peak hold then 24 dB/s decay. */
    fall = dt * 180u + m->release_rem;
    m->release_rem = fall % 1000u;
    fall /= 1000u;
    m->level = m->level > fall ? m->level - fall : 0;
    if (input >= m->level) { m->level = input; m->release_rem = 0; }
    if (numeric >= m->peak) {
        m->peak=numeric;m->held = input; m->hold_ms = 750u; m->peak_rem = 0;
    } else if (dt <= m->hold_ms) {
        m->hold_ms -= dt;
    } else {
        fall = (dt - m->hold_ms) * 240u + m->peak_rem;
        m->hold_ms = 0; m->peak_rem = fall % 1000u; fall /= 1000u;
        m->held = m->held > fall ? m->held - fall : 0;
        if (m->held < m->level) m->held = m->level;
        m->peak=m->peak>fall?m->peak-fall:0;
        if(m->peak<numeric)m->peak=numeric;
        if(m->level && m->peak<m->level+GATEWAY_PEAK_OFFSET)m->peak=m->level+GATEWAY_PEAK_OFFSET;
    }
    m->clip_ms = m->clip_ms > dt ? m->clip_ms - dt : 0;
    if (peak >= 8388352u) m->clip_ms = 1000u;
    return level != m->level || held != m->held || previous_peak!=m->peak || clipped != (m->clip_ms != 0);
}
