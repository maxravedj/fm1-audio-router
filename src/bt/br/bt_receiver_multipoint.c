/* SPDX-License-Identifier: MPL-2.0 */
/* Two ACL sessions, one decoder. Radio frames and their cryptographic state
 * remain separate; CPU1 is joined before ownership of PCM changes. Register
 * provenance and the hardware validation checklist are in docs/BLUETOOTH.md. */
static unsigned br_peer_held(const uint8_t addr[6])
{
    for(unsigned i=0;i<4;i++)if((br_held_valid&(1u<<i)) && !memcmp(br_held_peers[i],addr,6))return i+1u;
    return 0;
}
static void br_hold_peer(int hold)
{
    brl.reconnect_hold=(uint8_t)hold;
    unsigned at=br_peer_held(brl.remote);
    if(hold){
        if(!at)at=1u+(br_held_next++&3u);
        memcpy(br_held_peers[at-1u],brl.remote,6);br_held_valid|=1u<<(at-1u);
    }else if(at)br_held_valid&=(uint8_t)~(1u<<(at-1u));
}
static unsigned br_selected_link(void)
{
    if(br_audio_link<BR_LINKS)return br_audio_link;
    for(unsigned i=0;i<BR_LINKS;i++)if(br_links[i].encrypted && br_links[i].setup)return i;
    return 0;
}
static void br_stream_claim(void)
{
    unsigned next=br_current,old=br_audio_link;
    /* A new accepted stream is fresh user activity. Retry a peer-closed
     * remote-control channel now, rather than during its idle disconnect. */
    if(!brc.ch[3].remote)brc.avrcp_sent=0;
    if(old==next){brc.pause=0;return;}
    if(old<BR_LINKS){
        /* Do not change the old AVDTP state ourselves: its SUSPEND still
         * needs a valid response. Late media is simply excluded by ownership. */
        if(br_channels[old].profile.state==BTR_STREAMING)br_channels[old].pause=1;
        br_current=(uint8_t)old;br_audio_flush(44100);br_current=(uint8_t)next;
    }
    br_audio_link=(uint8_t)next;brc.pause=0;
}
static void br_avrcp_poll(void)
{
    if(!brl.encrypted || !brl.setup || brl.closing || brl.reconnect_hold || !brc.profile_ready)return;
    uint8_t out[16];
    if(!brc.ch[3].remote && !brc.avrcp_sent && fm1_ms-brl.secure_at>=1000){
        uint8_t id=(uint8_t)(brc.id+1u);if(!id)id=1;
        if(BTR_SEND(out,bl_conn_req(out,id,0x17,67)))
            brc.id=brc.avrcp_id=id,brc.avrcp_sent=1;
    }
    if(!brc.pause || !brc.ch[3].cfg_in || !brc.ch[3].cfg_out || !BTR_ROOM(2))return;
    /* AVCTP single command, AVRCP panel PASS THROUGH PAUSE. A press and a
     * release are distinct labelled transactions, never a held pause key. */
    out[0]=(uint8_t)((brc.label++&15u)<<4);out[1]=0x11;out[2]=0x0e;
    out[3]=0;out[4]=0x48;out[5]=0x7c;out[6]=brc.pause==1?0x46:0xc6;out[7]=0;
    br_send(3,out,8);brc.pause=brc.pause==1?2:0;
}
#include "bt_receiver_radio.c"
