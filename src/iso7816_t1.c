#include "iso7816/iso7816_t1.h"

#include <string.h>

#include "iso7816/iso7816_apdu.h"
#include "iso7816/iso7816_char.h"
#include "iso7816_internal.h"

/* ---- constantes du protocole (cl. 11) ---- */
#define T1_BGT_ETU   22U                       /* block guard time (cl. 11.2) */

/* S-block : fonctions (bits 5..1 de PCB, bit 6 = réponse) — cl. 11.3.2.2 */
#define T1_S_RESYNCH 0x00
#define T1_S_IFS     0x01
#define T1_S_ABORT   0x02
#define T1_S_WTX     0x03

#define T1_R_ERR_EDC 0x01    /* erreur de redondance / parité (cl. 11.3.2.2) */
#define T1_R_ERR_OTHER 0x02

static uint32_t t1_cwt_us(const iso7816_t *d)
{
    /* CWT = (11 + 2^CWI) etu, min 12 etu (cl. 11.4.3) */
    return (11U + (1U << d->cwi)) * iso7816_etu_us(d);
}

static uint32_t t1_bwt_us(const iso7816_t *d)
{
    /* BWT = 11 etu + 2^BWI * 960 * Fd/f, Fd = 372 (cl. 11.4.3) */
    uint32_t etu = iso7816_etu_us(d);
    uint64_t extra = ((uint64_t)(1U << d->bwi) * 960ULL * 372ULL * 1000000ULL) / d->f_hz;
    return 11U * etu + (uint32_t)extra;
}

/* CRC-16 selon ISO/IEC 13239 (poly 0x1021, init 0xFFFF) — cl. 11.3.4 */
static uint16_t t1_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static bool t1_is_iblock(uint8_t pcb) { return (pcb & 0x80) == 0; }
static bool t1_is_rblock(uint8_t pcb) { return (pcb & 0xC0) == 0x80; }
static bool t1_is_sblock(uint8_t pcb) { return (pcb & 0xC0) == 0xC0; }

static iso7816_status_t t1_send_block(iso7816_t *d, uint8_t nad, uint8_t pcb,
                                       const uint8_t *inf, uint8_t len)
{
    if (d->last_dir_rx) {
        /* BGT entre le dernier caractère reçu et le premier émis (cl. 11.2) */
        iso7816_wait_until(d, d->last_rx_lead_us + T1_BGT_ETU * iso7816_etu_us(d));
        d->last_dir_rx = false;
    }

    uint8_t buf[3 + ISO7816_T1_INF_MAX];
    buf[0] = nad;
    buf[1] = pcb;
    buf[2] = len;
    if (len > 0) memcpy(buf + 3, inf, len);
    size_t n = 3 + len;

    for (size_t i = 0; i < n; i++) {
        iso7816_status_t st = iso7816_char_tx(d, buf[i]);
        if (st != ISO7816_OK) return st;
    }

    if (d->use_crc) {
        uint16_t c = t1_crc16(buf, n);
        iso7816_status_t st = iso7816_char_tx(d, (uint8_t)(c >> 8));
        if (st != ISO7816_OK) return st;
        st = iso7816_char_tx(d, (uint8_t)(c & 0xFF));
        if (st != ISO7816_OK) return st;
    } else {
        uint8_t lrc = 0;
        for (size_t i = 0; i < n; i++) lrc ^= buf[i];
        iso7816_status_t st = iso7816_char_tx(d, lrc);
        if (st != ISO7816_OK) return st;
    }
    return ISO7816_OK;
}

static iso7816_status_t t1_recv_byte(iso7816_t *d, uint8_t *b, uint32_t timeout_us)
{
    return iso7816_char_rx(d, b, timeout_us);
}

/*
 * Reçoit un bloc. timeout_us s'applique au premier caractère (BWT).
 * Retourne ISO7816_CHAR_WT si la carte est muette, ISO7816_T1_EDC si le
 * bloc est invalide (parité, LEN, LRC/CRC — cl. 11.6.3.1).
 */
static iso7816_status_t t1_recv_block(iso7816_t *d, iso7816_t1_block_t *blk,
                                       uint32_t timeout_us)
{
    uint8_t b;
    iso7816_status_t st = t1_recv_byte(d, &b, timeout_us);
    if (st != ISO7816_OK) return st;           /* CHAR_WT = BWT dépassé */
    blk->nad = b;
    d->last_rx_lead_us = iso7816_now(d);
    d->last_dir_rx = true;

    uint32_t cwt = t1_cwt_us(d);
    st = t1_recv_byte(d, &b, cwt);
    if (st != ISO7816_OK) return ISO7816_T1_EDC;
    blk->pcb = b;
    st = t1_recv_byte(d, &b, cwt);
    if (st != ISO7816_OK) return ISO7816_T1_EDC;
    blk->len = b;
    blk->inf_len = b;
    if (b > ISO7816_T1_INF_MAX) return ISO7816_T1_EDC;

    uint8_t x = blk->nad ^ blk->pcb ^ blk->len;
    for (uint8_t i = 0; i < b; i++) {
        st = t1_recv_byte(d, &blk->inf[i], cwt);
        if (st != ISO7816_OK) return ISO7816_T1_EDC;
        x ^= blk->inf[i];
    }

    if (d->use_crc) {
        uint8_t hi, lo;
        st = t1_recv_byte(d, &hi, cwt);
        if (st != ISO7816_OK) return ISO7816_T1_EDC;
        st = t1_recv_byte(d, &lo, cwt);
        if (st != ISO7816_OK) return ISO7816_T1_EDC;
        uint8_t tmp[3 + ISO7816_T1_INF_MAX];
        tmp[0] = blk->nad; tmp[1] = blk->pcb; tmp[2] = blk->len;
        memcpy(tmp + 3, blk->inf, b);
        uint16_t c = t1_crc16(tmp, 3 + b);
        if (c != (uint16_t)((hi << 8) | lo)) return ISO7816_T1_EDC;
    } else {
        uint8_t lrc;
        st = t1_recv_byte(d, &lrc, cwt);
        if (st != ISO7816_OK) return ISO7816_T1_EDC;
        if ((uint8_t)(x ^ lrc) != 0) return ISO7816_T1_EDC;
    }
    return ISO7816_OK;
}

static iso7816_status_t t1_send_r(iso7816_t *d, uint8_t n_r, uint8_t err)
{
    uint8_t pcb = (uint8_t)(0x80 | ((n_r & 1) << 5) | (err & 0x1F));
    return t1_send_block(d, 0x00, pcb, NULL, 0);
}

static iso7816_status_t t1_send_s(iso7816_t *d, uint8_t func, bool response,
                                  const uint8_t *inf, uint8_t len)
{
    uint8_t pcb = (uint8_t)(0xC0 | (response ? 0x20 : 0) | (func & 0x1F));
    return t1_send_block(d, 0x00, pcb, inf, len);
}

/* RESYNCH : max 3 tentatives (règle 6.4, cl. 11.6.3.2) */
static iso7816_status_t t1_resynch(iso7816_t *d)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        iso7816_status_t st = t1_send_s(d, T1_S_RESYNCH, false, NULL, 0);
        if (st != ISO7816_OK) return st;
        iso7816_t1_block_t blk;
        st = t1_recv_block(d, &blk, t1_bwt_us(d));
        if (st == ISO7816_OK && t1_is_sblock(blk.pcb) &&
            (blk.pcb & 0x1F) == T1_S_RESYNCH && (blk.pcb & 0x20)) {
            d->ns_ifd = 0;
            d->ns_card = 0;
            return ISO7816_OK;
        }
    }
    return ISO7816_T1_RESYNC;
}

/* Envoie la commande APDU en I-blocs chaînés si nécessaire (bit M, cl. 11.6.2.2). */
static iso7816_status_t t1_send_apdu(iso7816_t *d, const uint8_t *cmd, size_t cmd_len)
{
    size_t off = 0;
    do {
        size_t chunk = cmd_len - off;
        if (chunk > d->ifsc) chunk = d->ifsc;
        bool more = (off + chunk) < cmd_len;
        uint8_t pcb = (uint8_t)(((d->ns_ifd & 1) << 6) | (more ? 0x20 : 0x00));
        iso7816_status_t st = t1_send_block(d, 0x00, pcb, cmd + off, (uint8_t)chunk);
        if (st != ISO7816_OK) return st;
        d->ns_ifd ^= 1;
        off += chunk;
        if (more) {
            /* acquittement R(N(R) = N(S) attendu) — règle 2.2 */
            iso7816_t1_block_t blk;
            st = t1_recv_block(d, &blk, t1_bwt_us(d));
            if (st != ISO7816_OK) return st;
            if (!t1_is_rblock(blk.pcb) || ((blk.pcb >> 5) & 1) != d->ns_ifd ||
                (blk.pcb & 0x1F) != 0) {
                return ISO7816_T1_SEQ;
            }
        }
    } while (off < cmd_len);
    return ISO7816_OK;
}

/* Reçoit la réponse (I-blocs jusqu'à M=0). Données accumulées dans resp. */
static iso7816_status_t t1_recv_response(iso7816_t *d,
                                         uint8_t *resp, size_t cap, size_t *recvd,
                                         uint16_t *sw)
{
    size_t n = 0;
    uint8_t expected_ns = d->ns_card;
    uint32_t bwt = t1_bwt_us(d);
    int retries = 0;

    for (;;) {
        iso7816_t1_block_t blk;
        iso7816_status_t st = t1_recv_block(d, &blk, bwt);

        if (st == ISO7816_CHAR_WT) {
            /* BWT dépassé : R(N(R)) puis max 2 retries, sinon RESYNCH (règles 7.1/7.4) */
            if (++retries > 2) {
                iso7816_status_t rs = t1_resynch(d);
                return (rs == ISO7816_OK) ? ISO7816_T1_BWT : rs;
            }
            st = t1_send_r(d, expected_ns, 0);
            if (st != ISO7816_OK) return st;
            continue;
        }
        if (st != ISO7816_OK) {
            /* bloc invalide : R avec code erreur 0001, max 2 retries (règle 7.1/7.4.2) */
            if (++retries > 2) {
                iso7816_status_t rs = t1_resynch(d);
                return (rs == ISO7816_OK) ? ISO7816_T1_EDC : rs;
            }
            st = t1_send_r(d, expected_ns, T1_R_ERR_EDC);
            if (st != ISO7816_OK) return st;
            continue;
        }
        retries = 0;

        if (t1_is_sblock(blk.pcb)) {
            uint8_t func = blk.pcb & 0x1F;
            bool is_resp = (blk.pcb & 0x20) != 0;
            if (func == T1_S_WTX && !is_resp) {
                /* S(WTX request) : acquitter et étendre BWT (règle 3) */
                iso7816_status_t s2 = t1_send_s(d, T1_S_WTX, true, blk.inf, blk.inf_len);
                if (s2 != ISO7816_OK) return s2;
                if (blk.inf_len >= 1) {
                    bwt = t1_bwt_us(d) * blk.inf[0];
                }
                continue;
            }
            if (func == T1_S_IFS && !is_resp) {
                iso7816_status_t s2 = t1_send_s(d, T1_S_IFS, true, blk.inf, blk.inf_len);
                if (s2 != ISO7816_OK) return s2;
                if (blk.inf_len >= 1) d->ifsc = blk.inf[0];
                continue;
            }
            if (func == T1_S_ABORT && !is_resp) {
                iso7816_status_t s2 = t1_send_s(d, T1_S_ABORT, true, NULL, 0);
                if (s2 != ISO7816_OK) return s2;
                return ISO7816_T1_SEQ;
            }
            continue;
        }

        if (t1_is_rblock(blk.pcb)) {
            /* acquittement (ou erreur) de notre dernier bloc : on continue */
            if ((blk.pcb & 0x1F) != 0) return ISO7816_T1_EDC;
            continue;
        }

        if (t1_is_iblock(blk.pcb)) {
            uint8_t ns = (blk.pcb >> 6) & 1;
            if (ns != expected_ns) {
                /* erreur de séquence : R(N(R)=attendu) code 0010 (cl. 11.3.2.2) */
                if (++retries > 2) return ISO7816_T1_SEQ;
                iso7816_status_t s2 = t1_send_r(d, expected_ns, T1_R_ERR_OTHER);
                if (s2 != ISO7816_OK) return s2;
                continue;
            }
            if (n + blk.inf_len > cap) return ISO7816_GEN_SIZE;
            memcpy(resp + n, blk.inf, blk.inf_len);
            n += blk.inf_len;
            expected_ns ^= 1;
            d->ns_card = expected_ns;
            bool more = (blk.pcb & 0x20) != 0;
            /* acquittement du bloc reçu (règles 2.1/2.2) */
            iso7816_status_t s2 = t1_send_r(d, expected_ns, 0);
            if (s2 != ISO7816_OK) return s2;
            if (!more) break;
        }
    }

    if (n < 2) return ISO7816_T1_SEQ;
    *sw = (uint16_t)((resp[n - 2] << 8) | resp[n - 1]);
    *recvd = n - 2;
    return ISO7816_OK;
}

iso7816_status_t iso7816_t1_exchange(iso7816_t *d,
                                     const uint8_t *cmd, size_t cmd_len,
                                     uint8_t *resp, size_t resp_cap,
                                     size_t *resp_len, uint16_t *sw)
{
    if (!d || !d->active || d->protocol != 1) return ISO7816_GEN_STATE;

    iso7816_status_t st = t1_send_apdu(d, cmd, cmd_len);
    if (st != ISO7816_OK) return st;

    size_t recvd = 0;
    uint16_t cur_sw = 0;
    st = t1_recv_response(d, resp, resp_cap, &recvd, &cur_sw);
    if (st != ISO7816_OK) return st;

    /* '61xx' : GET RESPONSE par T=1 (les données suivent en I-blocs chaînés) */
    unsigned guard = 0;
    while ((cur_sw & 0xFF00) == 0x6100 && guard++ < 16) {
        uint8_t more = (uint8_t)(cur_sw & 0xFF);   /* 0 = 256 */
        uint8_t p3 = more;                          /* P3 = octets restants demandés */
        uint8_t gr[5] = { 0x00, 0xC0, 0x00, 0x00, p3 };
        st = t1_send_apdu(d, gr, sizeof(gr));
        if (st != ISO7816_OK) return st;
        size_t rl = 0;
        st = t1_recv_response(d, resp + recvd, resp_cap - recvd, &rl, &cur_sw);
        if (st != ISO7816_OK) return st;
        recvd += rl;
    }

    *resp_len = recvd;
    *sw = cur_sw;
    return ISO7816_OK;
}