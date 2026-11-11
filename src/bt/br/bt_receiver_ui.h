/* SPDX-License-Identifier: MPL-2.0 */
/* A foreground checkpoint for long LCD/graph operations. Unlike bti_poll,
 * this cannot process signalling (which can free a graph via AAC acceptance,
 * join a worker or reset a stream). Preserve RX order: stop at control traffic
 * and let the normal main-loop poll handle it after the current UI operation.
 * CPU0 is the sole consumer; radio IRQs only publish at bti_rw. */
#pragma once
#ifdef APP_BT_RESAMPLE_CPU2
static int br_ui_media_ready(void)
{
    if(!brl.on || brl.closing || brl.phase!=BR_LINKED || !brl.setup ||
       !brl.encrypted || brc.profile.state!=BTR_STREAMING || !btr_worker_enabled)return 0;
    /* Media handlers allocate lazily. Only enter after the ordinary foreground
     * poll has prepared every required buffer. Nothing here can invalidate it. */
    switch(brc.profile.codec) {
    case BTR_CODEC_SBC:return btr_sbc!=0;
#ifdef APP_BT_AAC
    case BTR_CODEC_AAC:return btr_aac && btr_aac->workspace;
#endif
#ifdef APP_BT_APTX
    case BTR_CODEC_APTX:case BTR_CODEC_APTX_HD:return btr_aptx!=0;
#endif
#ifdef APP_BT_LDAC
    case BTR_CODEC_LDAC:return btr_ldac!=0;
#endif
    default:return 0;
    }
}
void plat_bt_receiver_service(void)
{
    static uint32_t last_ms;
    uint32_t now=plat_ms();
    if(now==last_ms || !br_ui_media_ready())return;
    last_ms=now;
    for(unsigned i=0;i<8u && BTI_RING_HAVE && bti_rr!=bti_rw;i++) {
        RING_PUBLISH();
        const struct bti_rx *r=BTI_RX_AT(bti_rr);
        if(r->kind!=BTI_K_ACL || r->len>sizeof r->data)break;
        /* Media uses local CID 66. Continuations belong to the one existing
         * reassembly, whose header must already identify that same channel.
         * Short/unknown headers and all other traffic wait for bti_poll. */
        if(r->idx==2) {
            if(r->len<4u || br_le16(r->data+2)!=66u)break;
        }else if(r->idx!=1 || brc.have<4u || br_le16(brc.rx+2)!=66u)break;
        br_acl(r->idx,r->data,r->len);
        RING_PUBLISH();bti_rr++;
    }
    btr_decode_poll_irq(); /* Nonblocking CPU1 dispatch; still on CPU0 foreground. */
}
#else
void plat_bt_receiver_service(void) {}
#endif
