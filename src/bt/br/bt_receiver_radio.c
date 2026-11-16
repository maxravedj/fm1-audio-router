/* SPDX-License-Identifier: MPL-2.0 */
/* Radio scheduling shared verbatim by the receiver and paced decoder tests. */
/* Inhibit new exchanges, then leave six slots for a five-slot packet and its
 * response to finish before changing the active frame. No spin/wait in IRQ. */
static void br_radio_quiet(uint32_t now)
{
    for(unsigned i=0;i<16;i++)fm1_br_xchg(bti_tab,i,FM1_BR_OFF_FRAME+180u*br_radio+4096u);
    br_switching=1;br_slice_at=now;bti_alarm_arm(6);
}
static int br_frame_safe(void)
{
    return br_current!=br_radio || (brl.closing && br_links[br_current^1u].closing &&
        br_switching && ((bti_clkn()-br_slice_at)&BTI_CLK)>=6u);
}
static unsigned br_other_radio(void)
{
    unsigned other=br_radio^1u;
    return br_links[other].on && !br_links[other].closing?other:br_radio;
}
#ifndef BR_RADIO_BUFFERED
#ifdef APP_BT_RESAMPLE_CPU2
/* Permanent PCM cursors only: the radio IRQ must never dereference a decoder
 * workspace which foreground can join/free. CPU0 owns r; CPU1 publishes w. */
#define BR_RADIO_BUFFERED() ((uint32_t)(btr_out_w-btr_out_r)>=1024u)
#else
#define BR_RADIO_BUFFERED() 1
#endif
#endif
static void br_schedule(uint32_t now)
{
    if(br_switching){
        if(((now-br_slice_at)&BTI_CLK)<6u){bti_alarm_arm(6);return;}
        /* If both ACLs close together, remain quiescent until foreground has
         * reset a drained frame to scan. Never resume a retired encrypted ACL. */
        if(br_links[0].closing && br_links[1].closing){bti_alarm_arm(8);return;}
        unsigned next=br_other_radio();br_radio=(uint8_t)next;br_current=(uint8_t)next;
        br_switching=0;br_slice_at=now;
        br_exchange(brl.parity);
        fm1_br_eir_tx(brl.phase==BR_INQUIRY?23:0,brl.phase==BR_INQUIRY?10:0,BTI_OFF_EIR_TX);
        bti_alarm_arm(8);return;
    }
    unsigned other=br_radio^1u;
    if(!br_links[other].on || br_links[other].closing){bti_alarm_arm(18);return;}
    /* Keep the time-critical page/FHS handshake on its frame. SSP and AVDTP
     * negotiation use the ordinary standby slices after the ACL is established. */
    if(brl.phase==BR_PAGE_RESPONSE || brl.phase==BR_FHS_ACK ||
       (brl.central && brl.phase<BR_CONNECTING && br_page_step) ||
       (brl.phase==BR_CONNECTING && fm1_ms-brl.started<40))return;
    uint32_t age=(now-br_slice_at)&BTI_CLK;
    unsigned slots=16u;
    if(ba_live && br_audio_link<BR_LINKS){
        if(br_radio==br_audio_link){
            /* A silent/empty slot must not take a quarter of the stream's
             * airtime. Visit it less often, after PCM can cover the absence.
             * Bound the extension so a starved source cannot deny handover. */
            slots=160u;
            if(age<320u && !brl.closing && !BR_RADIO_BUFFERED())slots=320u;
        }else if(brl.phase==BR_LINKED && brl.encrypted && blt.w==blt.r)slots=8u;
        else if(brl.phase<BR_PAGE_RESPONSE && !brl.central && !brl.pairing)slots=8u;
    }
    if(age>=slots || brl.closing)br_radio_quiet(now);
    else bti_alarm_arm(8);
}
