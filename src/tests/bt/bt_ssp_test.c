/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Secure Simple Pairing, Just Works, the FM-1 as the responder (firmware/src/bt/bt_ssp.c): a simulated initiator (the device
 * that starts pairing, as the Raspberry Pi of docs/BT-CLASSIC.md 14 did) built from the spec functions (bt_safer.c, its
 * f1 f2 f3 E1 E3 checked against Part G's sample data in tests/bt_crypto_test.c) runs the whole exchange through the
 * LMP PDUs: IO capabilities, both P-192 keys in encapsulated PDUs, the commitment and both nonces, the DHKey checks,
 * the authentication (au_rand / sres), the encryption start. Both sides must agree: our commitment, the initiator's
 * and our DHKey checks, the link key, SRES, Kc. A wrong DHKey check from the device is refused. This proves the two
 * halves agree with each other and with the spec's functions; it does not prove a real device's byte order (that is
 * the hardware test). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../bt/br/bt_sha256.c"
#include "../../bt/br/bt_safer.c"
#include "../../bt/br/bt_lmp.c"
#include "../../bt/br/bt_ssp.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("bt_ssp: %-90s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t seed = 0x1234567u;
static int rng(uint8_t *d, unsigned n)                  /* (deterministic: the test repeats) */
{
    unsigned i;
    for (i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        d[i] = (uint8_t)(seed >> 16);
    }
    return 1;
}
/* what the FM-1 sent, in order */
static uint8_t out[64][BTL_MAX];
static uint32_t outl[64], nout, rd;
static void sink(void *ctx, const uint8_t *pdu, uint32_t len)
{
    (void)ctx;
    memcpy(out[nout], pdu, len);
    outl[nout++] = len;
}
static const uint8_t *next(uint32_t op, uint32_t *len)   /* the next PDU we sent: it must be op */
{
    uint32_t tid;
    if (rd >= nout || btl_opcode(out[rd], outl[rd], &tid) != op)
        return 0;
    *len = outl[rd];
    return out[rd++] + (op >> 8 == BTL_ESCAPE ? 2 : 1);
}
static void rev(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        d[i] = s[n - 1u - i];
}
static struct btp fm;
static uint8_t dev_tid=1;
static void from_dev(const uint8_t *pdu, uint32_t len) { (void)btp_in(&fm, pdu, len); }
static void dev_pdu(uint32_t op, const uint8_t *par, uint32_t n)   /* the initiator sends op (TID 1: the slave's) */
{
    uint8_t b[BTL_MAX];
    uint32_t k = 0;
    if (op >> 8 == BTL_ESCAPE)
        b[k++] = (uint8_t)(BTL_ESCAPE << 1 | dev_tid), b[k++] = (uint8_t)op;
    else
        b[k++] = (uint8_t)(op << 1 | dev_tid);
    memcpy(b + k, par, n);
    from_dev(b, k + n);
}
static int accepted(uint32_t of)
{
    uint32_t len;
    const uint8_t *p = next(BTL_ACCEPTED, &len);
    return p && p[0] == of;
}

static void run(int bad_check,int peripheral)
{
    const uint8_t own[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, peer[6] = {0x93, 0x2a, 0xde, 0xdd, 0x3a, 0xd8};
    uint8_t apub[64], apriv[32], a_pub_spec[48], b_pub_spec[48], b_air[48], dh_le[32], dhkey[24];
    uint8_t na[16], na_air[16], nb[16], cb[16], cb_ok[16], iocap_a[3] = {3, 0, 4}, iocap_b[3], lk[16], lk_air[16];
    uint8_t a1[6], a2[6], r0[16] = {0}, e[16], e_air[16], au[16], sres[4], aco[12], kc[16];
    uint32_t len, i, ok = 1;
    const uint8_t *p;
    nout = rd = 0;
    btp_begin(&fm, own, peer, rng, sink, 0);
    fm.peripheral=(uint8_t)(peripheral!=0);dev_tid=peripheral?0:1;
    if(peripheral==2) { /* Phone forgot its bond: fresh SSP must supersede our kept key. */
        uint8_t old_key[16];rng(old_key,16);btp_bond(&fm,old_key);fm.init=0;
        btp_auth(&fm);nout=rd=0; /* Incoming SSP supersedes an outstanding reconnect challenge. */
    }
    uECC_set_rng(rng);
    bts_ecc_make_key(apub, apriv);                      /* the initiator's key pair */
    for (i = 0; i < 24u; i++)
        a_pub_spec[i] = apub[23u - i], a_pub_spec[24u + i] = apub[32u + 23u - i];
    /* 1. IO capabilities */
    dev_pdu(BTL_ESCAPE << 8 | BTL_X_IO_CAPABILITY_REQ, iocap_a, 3);
    p = next(BTL_ESCAPE << 8 | BTL_X_IO_CAPABILITY_RES, &len);
    check("the device's IO capability request: ours back, NoInputNoOutput (Just Works)", p && p[0] == 3u);
    if(peripheral)check("fresh peripheral: IO capability exchange clears saved-key mode",!fm.bonded && !fm.init && !fm.auth_pending);
    if (!p)
        return;
    memcpy(iocap_b, p, 3);
    /* 2. its public key: the header, three payloads, each accepted; then ours, each of ours accepted by it */
    {
        uint8_t h[3] = {1, 1, 48}, air[48];
        rev(air, a_pub_spec, 24), rev(air + 24, a_pub_spec + 24, 24);
        dev_pdu(BTL_ENCAPSULATED_HEADER, h, 3);
        ok &= accepted(BTL_ENCAPSULATED_HEADER);
        for (i = 0; i < 3u; i++) {
            dev_pdu(BTL_ENCAPSULATED_PAYLOAD, air + 16u * i, 16);
            ok &= accepted(BTL_ENCAPSULATED_PAYLOAD);
        }
        p = next(BTL_ENCAPSULATED_HEADER, &len);
        ok &= p && p[0] == 1u && p[1] == 1u && p[2] == 48u;
        for (i = 0; i < 3u; i++) {
            uint8_t a = (uint8_t)(i ? BTL_ENCAPSULATED_PAYLOAD : BTL_ENCAPSULATED_HEADER);
            dev_pdu(BTL_ACCEPTED, &a, 1);
            p = next(BTL_ENCAPSULATED_PAYLOAD, &len);
            ok &= p != 0;
            if (p)
                memcpy(b_air + 16u * i, p, 16);
        }
        rev(b_pub_spec, b_air, 24), rev(b_pub_spec + 24, b_air + 24, 24);
    }
    check("both P-192 public keys exchanged in encapsulated PDUs (a header, 3 x 16 octets, each accepted)", ok);
    /* 3. our commitment, after its accept of our last payload */
    {
        uint8_t a = BTL_ENCAPSULATED_PAYLOAD;
        dev_pdu(BTL_ACCEPTED, &a, 1);
        p = next(BTL_SIMPLE_PAIRING_CONFIRM, &len);
        check("our commitment Cb follows the key exchange", p != 0);
        if (!p)
            return;
        rev(cb, p, 16);
    }
    {   /* the initiator's DHKey, from our key */
        uint8_t pb[64];
        for (i = 0; i < 24u; i++)
            pb[i] = b_pub_spec[23u - i], pb[32u + i] = b_pub_spec[47u - i];
        for (i = 24u; i < 32u; i++)
            pb[i] = pb[32u + i] = 0;
        bts_ecc_shared_secret(pb, apriv, dh_le);
        rev(dhkey, dh_le, 24);
    }
    check("both sides have the same DHKey", !memcmp(dhkey, fm.dhkey, 24));
    /* 4. the nonces: Na from it, accepted, then our Nb; it checks Cb = f1(PKbx, PKax, Nb, 0) */
    rng(na, 16);
    rev(na_air, na, 16);
    dev_pdu(BTL_SIMPLE_PAIRING_NUMBER, na_air, 16);
    ok = accepted(BTL_SIMPLE_PAIRING_NUMBER);
    p = next(BTL_SIMPLE_PAIRING_NUMBER, &len);
    ok &= p != 0;
    if (!p)
        return;
    rev(nb, p, 16);
    bts_ssp_f1(b_pub_spec, a_pub_spec, nb, 0, cb_ok);
    check("Na accepted, our Nb sent; our commitment checks: Cb = f1(PKbx, PKax, Nb, 0)", ok && !memcmp(cb, cb_ok, 16));
    /* 5. the DHKey checks: Ea = f3(DHKey, Na, Nb, 0, IOcapA, A, B) to us, then our Eb = f3(DHKey, Nb, Na, 0, IOcapB, B, A) */
    rev(a1, peer, 6), rev(a2, own, 6);
    {   /* (f3 takes IOcap in the spec's order: AuthReq, OOB, IO capability; the PDU carries it reversed) */
        uint8_t io[3] = {iocap_a[2], iocap_a[1], iocap_a[0]};
        bts_ssp_f3(dhkey, na, nb, r0, io, a1, a2, e);
    }
    if (bad_check)
        e[5] ^= 1u;
    rev(e_air, e, 16);
    dev_pdu(BTL_DHKEY_CHECK, e_air, 16);
    if (bad_check) {
        uint32_t l2;
        const uint8_t *q = next(BTL_NOT_ACCEPTED, &l2);
        check("a wrong DHKey check from the device: refused, authentication failure, the pairing failed",
              q && q[0] == BTL_DHKEY_CHECK && q[1] == BTL_ERR_AUTH_FAILURE && fm.st == BTP_FAILED);
        return;
    }
    ok = accepted(BTL_DHKEY_CHECK);
    p = next(BTL_DHKEY_CHECK, &len);
    ok &= p != 0;
    if (p) {
        uint8_t eb[16];
        rev(eb, p, 16);
        uint8_t io[3] = {iocap_b[2], iocap_b[1], iocap_b[0]};
        bts_ssp_f3(dhkey, nb, na, r0, io, a2, a1, e);
        ok &= !memcmp(eb, e, 16);
    }
    check("its DHKey check accepted, ours sent and right: Eb = f3(DHKey, Nb, Na, 0, IOcapB, B, A)", ok);
    bts_ssp_f2(dhkey, na, nb, a1, a2, lk);
    rev(lk_air, lk, 16);
    check("the same link key both sides: LK = f2(DHKey, Na, Nb, \"btlk\", A, B)", fm.st == BTP_PAIRED && !memcmp(lk_air, fm.lk, 16));
    /* 6. authentication: its au_rand, our SRES = E1(LK, AU_RAND, B) */
    rng(au, 16);
    dev_pdu(BTL_AU_RAND, au, 16);
    p = next(BTL_SRES, &len);
    bts_e1(lk_air, au, own, sres, aco);
    check("its au_rand: our SRES = E1(LK, AU_RAND, our address)", p && !memcmp(p, sres, 4) &&
          fm.st == (peripheral ? BTP_AUTHING : BTP_AUTHED));
    if(peripheral) {
        /* A fresh SSP phone waits here for the reverse authentication before
         * it will request encryption. The previous fixture skipped this half. */
        p=next(BTL_AU_RAND,&len);
        check("fresh peripheral: SRES precedes reverse AU_RAND in the Central's pairing transaction",
              p && len==17 && out[rd-1][0]==BTL_AU_RAND<<1);
        if(!p)return;
        bts_e1(lk_air,p,peer,sres,aco);
        dev_pdu(BTL_SRES,sres,4);
        check("fresh peripheral: verifies Central's SRES, retains reverse ACO and waits for Central encryption",
              fm.st==BTP_AUTHED && !memcmp(fm.aco,aco,12) && nout==rd);
    }
    /* 7. encryption: its mode request accepted; our key size (16) and, once taken, EN_RAND; Kc = E3(LK, EN_RAND, ACO) */
    if(peripheral) {
        uint8_t on=1,size=16,en_rand[16];
        for(i=0;i<nout;i++)ok&=!(out[i][0]&1);
        if(peripheral==2) {
            btp_peripheral_encrypt(&fm);
            p=next(BTL_ENCRYPTION_MODE_REQ,&len);
            check("fresh incoming pairing: Peripheral initiates encryption with TID 1 after mutual authentication",
                  p && len==2 && p[0]==1 && (out[rd-1][0]&1) && fm.st==BTP_MODEREQ);
            dev_tid=1;on=BTL_ENCRYPTION_MODE_REQ;dev_pdu(BTL_ACCEPTED,&on,1);
            ok&=fm.st==BTP_KEYSIZE && nout==rd;
        } else {dev_pdu(BTL_ENCRYPTION_MODE_REQ,&on,1);ok&=accepted(BTL_ENCRYPTION_MODE_REQ);}
        dev_pdu(BTL_ENCRYPTION_KEY_SIZE_REQ,&size,1);ok&=accepted(BTL_ENCRYPTION_KEY_SIZE_REQ);
        rng(en_rand,16);bts_e3(lk_air,en_rand,aco,kc);
        dev_pdu(BTL_START_ENCRYPTION_REQ,en_rand,16);ok&=accepted(BTL_START_ENCRYPTION_REQ);
        check("fresh incoming pairing: both keys/nonces/DH checks, SRES and peripheral E0 encryption",
              ok && fm.st==BTP_ENCRYPTED && !memcmp(kc,fm.kc,16));
        check("fresh incoming pairing: final ACCEPTED retains the encryption initiator's TID",
              (out[rd-1][0]&1)==(peripheral==2));
    } else {
        uint8_t on = 1, a;
        dev_pdu(BTL_ENCRYPTION_MODE_REQ, &on, 1);
        ok = accepted(BTL_ENCRYPTION_MODE_REQ);
        p = next(BTL_ENCRYPTION_KEY_SIZE_REQ, &len);
        ok &= p && p[0] == 16u;
        a = BTL_ENCRYPTION_KEY_SIZE_REQ;
        dev_pdu(BTL_ACCEPTED, &a, 1);
        p = next(BTL_START_ENCRYPTION_REQ, &len);
        ok &= p != 0;
        if (p)
            bts_e3(lk_air, p, aco, kc);
        a = BTL_START_ENCRYPTION_REQ;
        dev_pdu(BTL_ACCEPTED, &a, 1);
        check("encryption: its request accepted, our key size 16 then start_encryption_req; Kc = E3(LK, EN_RAND, ACO) both sides",
              ok && !memcmp(kc, fm.kc, 16) && fm.st == BTP_ENCRYPTED);
    }
    check("every PDU we sent was looked at (none left over)", rd == nout);
}

/* the FM-1 as the initiator (btp_start: headphones refuse the audio channel until the link is paired and encrypted,
 * "security block", the WH-1000XM4 2026-10-20): our initiator against our responder, PDU by PDU; the encryption steps
 * as a slave answers them (the responder's own encryption path is the master's) */
static struct btp ia, rb;
static uint8_t qa[64][BTL_MAX], qb[64][BTL_MAX];
static uint32_t qal[64], qbl[64], na_, nb_, ra_, rb_;
static void to_b(void *ctx, const uint8_t *pdu, uint32_t len) { (void)ctx; memcpy(qa[na_], pdu, len); qal[na_++] = len; }
static void to_a(void *ctx, const uint8_t *pdu, uint32_t len) { (void)ctx; memcpy(qb[nb_], pdu, len); qbl[nb_++] = len; }
static uint32_t last_a_op;                              /* the last PDU A sent (its opcode) */
static void pump(void)                                  /* both queues delivered until nothing moves */
{
    uint32_t tid;
    while (ra_ < na_ || rb_ < nb_) {
        if (ra_ < na_) {
            last_a_op = btl_opcode(qa[ra_], qal[ra_], &tid);
            if (last_a_op == BTL_ENCRYPTION_MODE_REQ || last_a_op == BTL_ENCRYPTION_KEY_SIZE_REQ ||
                last_a_op == BTL_START_ENCRYPTION_REQ) {
                ra_++;
                return;                                 /* (the slave's answers: by hand) */
            }
            (void)btp_in(&rb, qa[ra_], qal[ra_]);
            ra_++;
        }
        if (rb_ < nb_) {
            (void)btp_in(&ia, qb[rb_], qbl[rb_]);
            rb_++;
        }
    }
}
static uint8_t dev_aco[12];                             /* the device's ACO from its own authentication: the COF after it */
static void device_authenticates(const uint8_t own[6], uint8_t sres[4])   /* its au_rand into A: A's SRES expected */
{
    uint8_t pdu[17];
    pdu[0] = (uint8_t)(BTL_AU_RAND << 1 | 1u);
    rng(pdu + 1, 16);
    bts_e1(ia.lk, pdu + 1, own, sres, dev_aco);
    (void)btp_in(&ia, pdu, 17);
    pump();
}
static void slave_accepts(uint32_t of)
{
    uint8_t b[2] = {(uint8_t)(BTL_ACCEPTED << 1 | 0u), (uint8_t)of};   /* (in our transaction: TID 0) */
    (void)btp_in(&ia, b, 2);
}
/* offer: 0 the slave takes our 16 octets; else it offers that key size instead (the Bose SoundLink Mini: 7, 2026-10-23),
 * taken from 7 with Kc' the reduced key, refused below */
static void run_initiator(uint32_t offer)
{
    const uint8_t own[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, peer[6] = {0x24, 0xed, 0x1f, 0x0a, 0x80, 0xac};
    uint8_t kc[16];
    na_ = nb_ = ra_ = rb_ = 0;
    uECC_set_rng(rng);
    btp_begin(&ia, own, peer, rng, to_b, 0);
    btp_begin(&rb, peer, own, rng, to_a, 0);
    btp_start(&ia);
    check("initiator: btp_start sends our IO capability (NoInputNoOutput)", na_ == 1u && ia.st == BTP_IOCAP);
    pump();
    check("initiator: keys, commitment, nonces and DHKey checks with a responder: both paired, the same link key",
          rb.st >= BTP_PAIRED && rb.st != BTP_FAILED && !memcmp(ia.lk, rb.lk, 16) && !memcmp(ia.dhkey, rb.dhkey, 24));
    check("initiator: then we authenticate it (our au_rand, its SRES checked); its authentication of us awaited, no encryption yet",
          ia.st == BTP_AUTHED && last_a_op == BTL_AU_RAND && !memcmp(ia.aco, rb.aco, 12));
    {   /* (Part C 4.2.5.1: the encryption not before the mutual authentication is done. Asked while the AirPods' au_rand
         * was on its way, they never kept the bond, 2026-10-21) */
        uint8_t sres[4];
        device_authenticates(own, sres);
        check("initiator: its au_rand answered (SRES = E1(LK, AU_RAND, our address)), then the encryption asked for",
              ia.st == BTP_MODEREQ && last_a_op == BTL_ENCRYPTION_MODE_REQ && !memcmp(qa[na_ - 2u] + 1, sres, 4));
    }
    slave_accepts(BTL_ENCRYPTION_MODE_REQ);
    check("initiator: its accept of the mode: our key size request (16)", ia.st == BTP_KEYSIZE &&
          btl_opcode(qa[na_ - 1u], qal[na_ - 1u], &(uint32_t){0}) == BTL_ENCRYPTION_KEY_SIZE_REQ && qa[na_ - 1u][1] == 16u);
    if (offer) {
        uint8_t b[2] = {(uint8_t)(BTL_ENCRYPTION_KEY_SIZE_REQ << 1 | 0u), (uint8_t)offer}, kc16[16];
        uint32_t at = na_;
        (void)btp_in(&ia, b, 2);
        if (offer < 7u) {
            check("initiator: a key size under 7 offered: refused, failed", ia.st == BTP_FAILED && na_ == at + 1u &&
                  btl_opcode(qa[at], qal[at], &(uint32_t){0}) == BTL_NOT_ACCEPTED);
            return;
        }
        check("initiator: its key size of 7 offered: accepted, then start_encryption_req", na_ == at + 2u &&
              btl_opcode(qa[at], qal[at], &(uint32_t){0}) == BTL_ACCEPTED && qa[at][1] == BTL_ENCRYPTION_KEY_SIZE_REQ);
        bts_e3(rb.lk, qa[na_ - 1u] + 1, dev_aco, kc16);
        bts_kc_reduce(kc16, offer, kc);
        check("initiator: Kc' = Kc reduced to 7 octets (bts_kc_reduce), not Kc",
              ia.st == BTP_STARTING && !memcmp(kc, ia.kc, 16) && memcmp(kc16, ia.kc, 16));
    } else {
        slave_accepts(BTL_ENCRYPTION_KEY_SIZE_REQ);
        bts_e3(rb.lk, qa[na_ - 1u] + 1, dev_aco, kc);  /* (the slave's Kc from our EN_RAND and the ACO of the latest
                                                         * authentication, its own: the COF, Part H 4.2.4) */
        check("initiator: then start_encryption_req; Kc = E3(LK, EN_RAND, ACO) the same both sides",
              ia.st == BTP_STARTING && !memcmp(kc, ia.kc, 16));
    }
    slave_accepts(BTL_START_ENCRYPTION_REQ);
    check("initiator: its accept: encrypted", ia.st == BTP_ENCRYPTED);
    {   /* a wrong SRES: the pairing fails (no encryption on a link we could not authenticate) */
        uint8_t b[5] = {(uint8_t)(BTL_SRES << 1 | 0u), 0, 0, 0, 0};
        ia.st = BTP_AUTHING;
        (void)btp_in(&ia, b, 5);
        check("initiator: a wrong SRES: failed", ia.st == BTP_FAILED);
    }
}

/* a bonded device (its link key kept from an earlier pairing, the FM-1's settings record): no pairing; our au_rand, its
 * SRES with the key checked, the encryption asked for; a device with another key fails (bt_acl.c then pairs afresh) */
static void run_bonded(int wrong_key)
{
    const uint8_t own[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, peer[6] = {0x24, 0xed, 0x1f, 0x0a, 0x80, 0xac};
    uint8_t lk[16], lk_dev[16];
    uint32_t i;
    na_ = nb_ = ra_ = rb_ = 0;
    rng(lk, 16);
    memcpy(lk_dev, lk, 16);
    if (wrong_key)
        lk_dev[3] ^= 0x40u;
    btp_begin(&ia, own, peer, rng, to_b, 0);
    btp_bond(&ia, lk);
    btp_begin(&rb, peer, own, rng, to_a, 0);           /* (the device: its kept key, as after a pairing) */
    for (i = 0; i < 16u; i++)
        rb.lk[i] = lk_dev[i];
    rb.st = BTP_PAIRED;
    btp_auth(&ia);
    pump();
    if (wrong_key)
        check("bonded, the device with another key: its SRES refused, failed", ia.st == BTP_FAILED);
    else
    {
        uint8_t sres[4];
        check("bonded: no pairing, our au_rand, its SRES with the kept key checked; its authentication of us awaited",
              ia.st == BTP_AUTHED && last_a_op == BTL_AU_RAND && !memcmp(ia.aco, rb.aco, 12));
        device_authenticates(own, sres);
        check("bonded: its au_rand answered with the kept key, then the encryption asked for",
              ia.st == BTP_MODEREQ && last_a_op == BTL_ENCRYPTION_MODE_REQ && !memcmp(qa[na_ - 2u] + 1, sres, 4));
    }
}

/* Incoming ACL: the peer is Central and drives encryption, TID 0. */
static void run_peripheral(void)
{
    uint8_t own[6]={1,2,3,4,5,6},peer[6]={6,5,4,3,2,1},lk[16],p[17],sres[4],aco[12],kc[16];
    nout=rd=0;rng(lk,16);btp_begin(&fm,own,peer,rng,sink,0);btp_bond(&fm,lk);
    fm.init=0;fm.peripheral=1;
    p[0]=BTL_AU_RAND<<1;rng(p+1,16);bts_e1(lk,p+1,own,sres,aco);
    check("peripheral: Central authenticates our saved bond",btp_in(&fm,p,17) && fm.st==BTP_AUTHED && nout==1 &&
        out[0][0]==BTL_SRES<<1 && !memcmp(out[0]+1,sres,4));
    p[0]=BTL_ENCRYPTION_MODE_REQ<<1;p[1]=1;
    check("peripheral: accepts encryption mode without sending Central's key-size request",btp_in(&fm,p,2) &&
        fm.st==BTP_KEYSIZE && nout==2 && out[1][1]==BTL_ENCRYPTION_MODE_REQ);
    p[0]=BTL_ENCRYPTION_KEY_SIZE_REQ<<1;p[1]=16;
    check("peripheral: accepts Central's 16-byte key size without starting encryption itself",btp_in(&fm,p,2) &&
        fm.st==BTP_KEYSIZE && nout==3 && out[2][1]==BTL_ENCRYPTION_KEY_SIZE_REQ);
    p[0]=BTL_START_ENCRYPTION_REQ<<1;rng(p+1,16);bts_e3(lk,p+1,aco,kc);
    check("peripheral: Central starts E0; matching Kc, encrypted ACCEPTED with Central TID",btp_in(&fm,p,17) &&
        fm.st==BTP_ENCRYPTED && nout==4 && !memcmp(fm.kc,kc,16) && out[3][0]==BTL_ACCEPTED<<1 &&
        out[3][1]==BTL_START_ENCRYPTION_REQ);
    nout=rd=0;btp_begin(&fm,own,peer,rng,sink,0);btp_bond(&fm,lk);fm.init=0;fm.peripheral=1;
    fm.st=BTP_KEYSIZE;fm.ksz=16;p[0]=BTL_ENCRYPTION_KEY_SIZE_REQ<<1;p[1]=6;
    check("peripheral: unsafe key-size offer refused",btp_in(&fm,p,2) && fm.st==BTP_FAILED);
    check("peripheral: empty and truncated escaped PDUs ignored",!btp_in(&fm,p,0));
}

static void run_peripheral_mutual_failure(void)
{
    uint8_t own[6]={1,2,3,4,5,6},peer[6]={6,5,4,3,2,1},lk[16],p[17],answer[4],aco[12],before[12];
    rng(lk,16);nout=rd=0;btp_begin(&fm,own,peer,rng,sink,0);
    fm.peripheral=1;fm.st=BTP_PAIRED;memcpy(fm.lk,lk,16);
    p[0]=BTL_AU_RAND<<1;rng(p+1,16);btp_in(&fm,p,17);
    check("fresh peripheral: publishes SRES then one reverse challenge, still unauthenticated",
          nout==2 && out[0][0]==BTL_SRES<<1 && out[1][0]==BTL_AU_RAND<<1 && fm.st==BTP_AUTHING);
    bts_e1(lk,fm.au,peer,answer,aco);memcpy(before,fm.aco,12);
    p[0]=BTL_SRES<<1;memcpy(p+1,answer,4);
    check("fresh peripheral: truncated SRES cannot complete authentication",!btp_in(&fm,p,4) && fm.st==BTP_AUTHING);
    p[0]|=1;
    check("fresh peripheral: wrong-transaction SRES ignored",!btp_in(&fm,p,5) && fm.st==BTP_AUTHING && !memcmp(before,fm.aco,12));
    p[0]=BTL_ENCRYPTION_MODE_REQ<<1;p[1]=1;
    check("fresh peripheral: early encryption cannot bypass missing reverse authentication",
          !btp_in(&fm,p,2) && fm.st==BTP_AUTHING && nout==2);
    p[0]=BTL_SRES<<1;memcpy(p+1,answer,4);p[1]^=1;btp_in(&fm,p,5);
    check("fresh peripheral: wrong SRES fails before encryption",fm.st==BTP_FAILED && fm.reason==BTL_ERR_AUTH_FAILURE && nout==2);
}

static void run_peripheral_encryption_request(void)
{
    uint8_t own[6]={1,2,3,4,5,6},peer[6]={6,5,4,3,2,1},lk[16],p[17],aco[12],kc[16],kc7[16];
    struct btp ready;
    nout=rd=0;rng(lk,16);btp_begin(&fm,own,peer,rng,sink,0);btp_bond(&fm,lk);
    fm.init=0;fm.peripheral=1;
    btp_peripheral_encrypt(&fm);
    check("Peripheral cannot request encryption before authentication",fm.st==BTP_PAIRED && !nout);
    p[0]=BTL_AU_RAND<<1;rng(p+1,16);btp_in(&fm,p,17);memcpy(aco,fm.aco,12);
    nout=rd=0;ready=fm;
    btp_peripheral_encrypt(&fm);btp_peripheral_encrypt(&fm);
    check("Peripheral requests encryption exactly once using its own TID",fm.st==BTP_MODEREQ && fm.tid==1 &&
          nout==1 && out[0][0]==((BTL_ENCRYPTION_MODE_REQ<<1)|1) && out[0][1]==1);
    p[0]=BTL_ACCEPTED<<1;p[1]=BTL_ENCRYPTION_MODE_REQ;
    check("mode ACCEPTED with wrong TID cannot advance Peripheral",!btp_in(&fm,p,2) && fm.st==BTP_MODEREQ && nout==1);
    p[0]|=1;btp_in(&fm,p,2);
    check("Central accepts Peripheral mode request; Peripheral waits without proposing a key size",fm.st==BTP_KEYSIZE && fm.tid==1 && nout==1);
    p[0]=(BTL_ACCEPTED<<1)|1;p[1]=BTL_ENCRYPTION_KEY_SIZE_REQ;
    check("unexpected key-size ACCEPTED cannot make Peripheral send START",!btp_in(&fm,p,2) && nout==1);
    p[0]=BTL_ENCRYPTION_KEY_SIZE_REQ<<1;p[1]=7;
    check("Central key-size offer with wrong TID ignored",!btp_in(&fm,p,2) && fm.ksz==16 && nout==1);
    p[0]|=1;btp_in(&fm,p,2);
    check("Peripheral accepts negotiated seven-byte size with original TID",fm.ksz==7 && nout==2 &&
          out[1][0]==((BTL_ACCEPTED<<1)|1) && out[1][1]==BTL_ENCRYPTION_KEY_SIZE_REQ);
    p[0]=BTL_AU_RAND<<1;rng(p+1,16);btp_in(&fm,p,17);
    check("authentication during encryption setup rejected without changing ACO or transaction",fm.st==BTP_KEYSIZE &&
          fm.tid==1 && !memcmp(fm.aco,aco,12) && nout==3 && out[2][0]==BTL_NOT_ACCEPTED<<1 && out[2][2]==0x24);
    p[0]=BTL_START_ENCRYPTION_REQ<<1;rng(p+1,16);bts_e3(lk,p+1,aco,kc);bts_kc_reduce(kc,7,kc7);
    check("wrong-TID START cannot load encryption key",!btp_in(&fm,p,17) && fm.st==BTP_KEYSIZE && nout==3);
    p[0]|=1;btp_in(&fm,p,17);
    check("Peripheral-initiated encryption derives reduced Kc from the Central's nonce",fm.st==BTP_ENCRYPTED &&
          !memcmp(fm.kc,kc7,16) && nout==4 && out[3][0]==((BTL_ACCEPTED<<1)|1));
    for(unsigned first=0;first<2;first++) {
        fm=ready;nout=rd=0;btp_peripheral_encrypt(&fm);
        if(first){p[0]=(BTL_NOT_ACCEPTED<<1)|1;p[1]=BTL_ENCRYPTION_MODE_REQ;p[2]=0x23;btp_in(&fm,p,3);
            check("collision rejection first: wait for Central without retrying our request",fm.st==BTP_AUTHED && nout==1);}
        p[0]=BTL_ENCRYPTION_MODE_REQ<<1;p[1]=1;btp_in(&fm,p,2);
        if(!first){p[0]=(BTL_NOT_ACCEPTED<<1)|1;p[1]=BTL_ENCRYPTION_MODE_REQ;p[2]=0x23;btp_in(&fm,p,3);}
        check("crossed mode requests: Central wins in either receive order",fm.st==BTP_KEYSIZE && fm.tid==0 && nout==2 && !fm.enc_pending);
        p[0]=BTL_ENCRYPTION_KEY_SIZE_REQ<<1;p[1]=16;btp_in(&fm,p,2);
        p[0]=BTL_START_ENCRYPTION_REQ<<1;rng(p+1,16);bts_e3(lk,p+1,aco,kc);btp_in(&fm,p,17);
        check("crossed mode requests finish encrypted in Central transaction",fm.st==BTP_ENCRYPTED &&
              !memcmp(fm.kc,kc,16) && nout==4 && !(out[3][0]&1));
    }
    fm=ready;nout=rd=0;btp_peripheral_encrypt(&fm);
    p[0]=(BTL_NOT_ACCEPTED<<1)|1;p[1]=BTL_ENCRYPTION_MODE_REQ;p[2]=BTL_ERR_AUTH_FAILURE;btp_in(&fm,p,3);
    check("a real encryption rejection remains fatal",fm.st==BTP_FAILED && fm.reason==BTL_ERR_AUTH_FAILURE);
}

/* A saved-bond incoming link can initiate legacy authentication itself. */
static void run_peripheral_bond_auth(void)
{
    const uint8_t own[6]={1,2,3,4,5,6},peer[6]={7,8,9,10,11,12};
    struct btp fm,challenging;uint8_t lk[16],p[17],sres[4],aco[12];
    rng(lk,16);nout=rd=0;btp_begin(&fm,own,peer,rng,sink,0);btp_bond(&fm,lk);fm.init=0;fm.peripheral=1;
    btp_auth(&fm);challenging=fm;
    check("saved Peripheral bond: initiates one AU_RAND in Peripheral transaction",fm.st==BTP_AUTHING && fm.tid==1 &&
          nout==1 && out[0][0]==((BTL_AU_RAND<<1)|1));
    bts_e1(lk,fm.au,peer,sres,aco);p[0]=BTL_SRES<<1;memcpy(p+1,sres,4);
    check("saved Peripheral bond: wrong-TID SRES cannot authenticate peer",!btp_in(&fm,p,5) && fm.st==BTP_AUTHING);
    p[0]|=1;btp_in(&fm,p,5);
    check("saved Peripheral bond: SRES proves key and supplies encryption ACO",fm.st==BTP_AUTHED && !fm.dev_authed &&
          !memcmp(fm.aco,aco,12));
    btp_peripheral_encrypt(&fm);
    check("saved Peripheral bond: requests encryption without waiting for Central challenge",fm.st==BTP_MODEREQ &&
          nout==2 && out[1][0]==((BTL_ENCRYPTION_MODE_REQ<<1)|1));
    for(unsigned reject_first=0;reject_first<2;reject_first++) {
        uint8_t collision[]={((BTL_NOT_ACCEPTED<<1)|1),BTL_AU_RAND,0x23};
        fm=challenging;nout=rd=0;
        if(reject_first){btp_in(&fm,collision,sizeof collision);check("auth collision rejection first waits for Central",fm.st==BTP_PAIRED);}
        p[0]=BTL_AU_RAND<<1;rng(p+1,16);bts_e1(lk,p+1,own,sres,aco);btp_in(&fm,p,17);
        check("auth collision: Central wins and supplies ACO in both receive orders",fm.st==BTP_AUTHED && fm.tid==0 &&
              nout==1 && out[0][0]==BTL_SRES<<1 && !memcmp(out[0]+1,sres,4) && !memcmp(fm.aco,aco,12));
        btp_peripheral_encrypt(&fm);
        if(!reject_first)btp_in(&fm,collision,sizeof collision);
        p[0]=(BTL_SRES<<1)|1;memset(p+1,0,4);
        check("auth collision: abandoned challenge SRES and late rejection cannot overwrite ACO",!btp_in(&fm,p,5) &&
              fm.st==BTP_MODEREQ && !memcmp(fm.aco,aco,12));
    }
    fm=challenging;nout=rd=0;p[0]=(BTL_SRES<<1)|1;bts_e1(lk,fm.au,peer,p+1,aco);p[1]^=1;btp_in(&fm,p,5);
    check("saved Peripheral bond: wrong SRES fails authentication",fm.st==BTP_FAILED && fm.reason==BTL_ERR_AUTH_FAILURE);
    fm=challenging;p[0]=(BTL_NOT_ACCEPTED<<1)|1;p[1]=BTL_AU_RAND;p[2]=6;btp_in(&fm,p,3);
    check("saved Peripheral bond: key missing rejection remains fatal",fm.st==BTP_FAILED && fm.reason==6);
}
int main(void)
{
    run(0,0);
    run(1,0);
    run(0,1);
    run(1,1);
    run(0,2);
    run_initiator(0);
    run_initiator(7);
    run_initiator(5);
    run_bonded(0);
    run_bonded(1);
    run_peripheral();
    run_peripheral_mutual_failure();
    run_peripheral_encryption_request();
    run_peripheral_bond_auth();
    printf("bt_ssp: %d failed\n", fails);
    return fails != 0;
}
