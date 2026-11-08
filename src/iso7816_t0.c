#include "iso7816/iso7816_t0.h"

#include "iso7816/iso7816_apdu.h"
#include "iso7816/iso7816_char.h"
#include "iso7816_internal.h"

static bool is_sw1(uint8_t b)
{
    return (b & 0xF0) == 0x60 || (b & 0xF0) == 0x90;   /* '6X' != '60' et '9X', cl. 10.3.3 */
}

static iso7816_status_t t0_rx(iso7816_t *d, uint8_t *b)
{
    for (uint8_t attempt = 0; attempt <= d->max_retries; attempt++) {
        iso7816_status_t st = iso7816_char_rx(d, b, d->wt_us);
        if (st == ISO7816_OK) return ISO7816_OK;
        if (st == ISO7816_CHAR_WT) return st;
    }
    return ISO7816_CHAR_PARITY;
}

/*
 * Un TPDU T=0 complet : header 5 octets + données entrantes/sortantes
 * pilotées par les octets de procédure (cl. 10.3), terminé par SW1-SW2.
 */
static iso7816_status_t t0_tpdu(iso7816_t *d,
                                uint8_t cla, uint8_t ins, uint8_t p1, uint8_t p2, uint8_t p3,
                                const uint8_t *tx, size_t tx_len,
                                uint8_t *rx, size_t rx_cap, size_t *rx_len,
                                uint16_t *sw)
{
    const uint8_t hdr[5] = { cla, ins, p1, p2, p3 };
    for (int i = 0; i < 5; i++) {
        iso7816_status_t st = iso7816_char_tx(d, hdr[i]);
        if (st != ISO7816_OK) return st;
    }

    size_t sent = 0;
    size_t recvd = 0;
    size_t expected_rx = p3;    /* octets de données attendus en sortie */

    for (;;) {
        uint8_t pb;
        iso7816_status_t st = t0_rx(d, &pb);
        if (st != ISO7816_OK) return st;

        if (pb == 0x60) continue;                       /* NULL */

        if (is_sw1(pb)) {                               /* SW1 -> attendre SW2 */
            uint8_t sw2;
            st = t0_rx(d, &sw2);
            if (st != ISO7816_OK) return st;
            *sw = (uint16_t)((pb << 8) | sw2);
            *rx_len = recvd;
            return ISO7816_OK;
        }

        if (pb == ins || pb == (uint8_t)(ins ^ 0xFF)) { /* ACK */
            if (sent < tx_len) {
                /* commande entrante : émettre les données restantes (ou 1 seule) */
                if (pb == ins) {
                    for (size_t i = sent; i < tx_len; i++) {
                        st = iso7816_char_tx(d, tx[i]);
                        if (st != ISO7816_OK) return st;
                    }
                    sent = tx_len;
                } else {
                    st = iso7816_char_tx(d, tx[sent++]);
                    if (st != ISO7816_OK) return st;
                }
                continue;
            }
            /* commande sortante : recevoir les données jusqu'à SW1-SW2
               (le SW1 '6X'/'9X' termine les données — cas '61xx', cl. 12.2) */
            for (;;) {
                if (recvd >= expected_rx && expected_rx > 0) {
                    /* P3 octets de données reçus : SW1-SW2 suivent */
                    uint8_t sw1, sw2;
                    st = t0_rx(d, &sw1);
                    if (st != ISO7816_OK) return st;
                    st = t0_rx(d, &sw2);
                    if (st != ISO7816_OK) return st;
                    *sw = (uint16_t)((sw1 << 8) | sw2);
                    *rx_len = recvd;
                    return ISO7816_OK;
                }
                uint8_t b;
                st = t0_rx(d, &b);
                if (st != ISO7816_OK) return st;
                if (is_sw1(b)) {
                    uint8_t sw2;
                    st = t0_rx(d, &sw2);
                    if (st != ISO7816_OK) return st;
                    *sw = (uint16_t)((b << 8) | sw2);
                    *rx_len = recvd;
                    return ISO7816_OK;
                }
                if (recvd >= rx_cap) return ISO7816_GEN_SIZE;
                rx[recvd++] = b;
                if (pb == (uint8_t)(ins ^ 0xFF)) break;   /* ACK partiel : 1 seul octet */
            }
            continue;
        }

        return ISO7816_T0_PROC;   /* octet de procédure invalide (cl. 10.3.3) */
    }
}

iso7816_status_t iso7816_t0_exchange(iso7816_t *d,
                                     const uint8_t *cmd, size_t cmd_len,
                                     uint8_t *resp, size_t resp_cap,
                                     size_t *resp_len, uint16_t *sw)
{
    if (!d || !d->active || d->protocol != 0) return ISO7816_GEN_STATE;

    iso7816_apdu_cmd_t c;
    iso7816_status_t st = iso7816_apdu_decode(cmd, cmd_len, &c);
    if (st != ISO7816_OK) return st;

    /* champs étendus non gérés par T=0 (ENVELOPE requis, hors périmètre) */
    if (c.apdu_case == ISO7816_APDU_CASE_2E || c.apdu_case == ISO7816_APDU_CASE_3E ||
        c.apdu_case == ISO7816_APDU_CASE_4E) {
        return ISO7816_APDU_INVALID;
    }

    bool incoming = (c.apdu_case == ISO7816_APDU_CASE_3S || c.apdu_case == ISO7816_APDU_CASE_4S);
    bool outgoing = (c.apdu_case == ISO7816_APDU_CASE_2S || c.apdu_case == ISO7816_APDU_CASE_4S);

    uint8_t p3;
    if (incoming) {
        p3 = (uint8_t)c.nc;             /* cas 3S/4S : nc <= 255 */
    } else if (outgoing) {
        p3 = (c.ne >= 256) ? 0 : (uint8_t)c.ne;
    } else {
        p3 = 0;
    }

    size_t total_rx = 0;
    uint16_t cur_sw = 0;

    for (unsigned guard = 0; guard < 8; guard++) {   /* re-émissions '6Cxx' */
        size_t rl = 0;
        st = t0_tpdu(d, c.cla, c.ins, c.p1, c.p2, p3,
                     incoming ? c.data : NULL, incoming ? c.nc : 0,
                     resp + total_rx, resp_cap - total_rx, &rl, &cur_sw);
        if (st != ISO7816_OK) return st;
        total_rx += rl;

        if (cur_sw == 0x9000 || (cur_sw & 0xFF00) == 0x6100 || (cur_sw & 0xFF00) == 0x6C00) {
            /* '6Cxx' : réémettre la même commande avec P3 = SW2 (cl. 12.2, tableau 14) */
            if ((cur_sw & 0xFF00) == 0x6C00) {
                p3 = cur_sw & 0xFF;
                continue;
            }
            break;
        }
        break;
    }

    /* '61xx' : données restantes via GET RESPONSE (cl. 12.2) */
    while ((cur_sw & 0xFF00) == 0x6100) {
        uint8_t more = cur_sw & 0xFF;               /* 0 = 256 */
        uint32_t want = c.ne ? c.ne : 256;
        uint32_t remaining = (want > total_rx) ? (want - total_rx) : 0;
        uint8_t p3g = (uint8_t)((remaining == 0) ? 0
                                : (more == 0 ? 0 : (more < remaining ? more : remaining)));
        if (p3g == 0 && more != 0) p3g = 0;         /* '00' = 256 */
        size_t rl = 0;
        st = t0_tpdu(d, 0x00, 0xC0, 0x00, 0x00, p3g,
                     NULL, 0, resp + total_rx, resp_cap - total_rx, &rl, &cur_sw);
        if (st != ISO7816_OK) return st;
        total_rx += rl;
        if ((cur_sw & 0xFF00) != 0x6100 && cur_sw != 0x9000) break;
        if (rl == 0) break;
    }

    *resp_len = total_rx;
    *sw = cur_sw;
    return ISO7816_OK;
}