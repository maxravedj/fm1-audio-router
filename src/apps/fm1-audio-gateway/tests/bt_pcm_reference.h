/* SPDX-License-Identifier: MPL-2.0 */
/* Frozen 0.0.17 mixer arithmetic, before CPU optimization. Full-width sums and
 * ordinary signed division form an independent bit-exact regression oracle.
 * PCM, producer index and sample rate are shared read-only with the DUT. */
static struct {
    uint32_t r,phase,step;
    uint8_t primed;
    int32_t servo;
    struct btr_counters stats;
} reference;
static void reference_mix(int32_t *lr,uint32_t n,uint32_t gain)
{
    uint32_t i,k,ch,r=reference.r,avail=btr_w-r,base=btr_rate==48000?71332:65536;
    int32_t error;
    if(!reference.primed){if(avail<BTR_TARGET)return;reference.primed=1;}
    /* About 140 ms buffered at 44.1k: main-loop flash commits/LCD stalls
     * must not immediately starve Bluetooth while SRAM audio IRQs keep running.
     * Low-bandwidth occupancy servo, limited to ±1000 ppm (66 Q16 units). */
    error=((int32_t)avail-(int32_t)BTR_TARGET)/16;
    if (error > 66) error = 66;
    if (error < -66) error = -66;
    reference.servo+=(error*256-reference.servo)/32;
    reference.step=base+reference.servo/256;
    for(i=0;i<n;i++) {
        int64_t sum[2] = {0,0};
        const int16_t (*filter)[96] = btr_rate == 48000 ? BTR_FIR_48 : BTR_FIR_44;
        uint32_t phase = reference.phase >> 10, frac = reference.phase & 1023u;
        if (btr_w-r < 98) { reference.stats.underruns++; reference.primed=0; reference.phase=0; break; }
        for (k=0;k<96;k++) {
            /* Interpolate coefficients between adjacent phases. This preserves
             * treble with fractional clocks without a huge coefficient table. */
            int32_t c = filter[phase][k] + ((filter[phase+1][k]-filter[phase][k])*(int32_t)frac+512)/1024;
            for (ch=0;ch<2;ch++) sum[ch] += (int32_t)btr_pcm[(r+k)&(BTR_RING-1)][ch]*c;
        }
        for (ch=0;ch<2;ch++) lr[2*i+ch] += (int32_t)((sum[ch]>>15)*256*gain/4096);
        reference.phase+=reference.step;r+=reference.phase>>16;reference.phase&=65535;
    }
    __asm__ volatile("" ::: "memory");reference.r=r;
}
