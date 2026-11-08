#include "iso7816/iso7816_t0.h"

#include "iso7816/iso7816_apdu.h"
#include "iso7816/iso7816_char.h"
#include "iso7816_internal.h"

static bool is_sw1(uint8_t byte)
{
    /* '6X' != '60' and '9X' (cl. 10.3.3) */
    return (byte & ISO7816_SW1_NIBBLE_MASK) == ISO7816_SW1_6X ||
           (byte & ISO7816_SW1_NIBBLE_MASK) == ISO7816_SW1_9X;
}

static iso7816_status_t t0_rx(iso7816_t *dev, uint8_t *byte)
{
    for (uint8_t attempt = 0; attempt <= dev->max_retries; attempt++) {
        iso7816_status_t stat = iso7816_char_rx(dev, byte, dev->wt_us);
        if (stat == ISO7816_OK) return ISO7816_OK;
        if (stat == ISO7816_CHAR_WT) return stat;
    }
    return ISO7816_CHAR_PARITY;
}

/*
 * Un TPDU T=0 complet : header 5 octets + données entrantes/sortantes
 * pilotées par les octets de procédure (cl. 10.3), terminé par SW1-SW2.
 */
static iso7816_status_t t0_tpdu(iso7816_t *dev,
                                uint8_t cla, uint8_t ins, uint8_t p1, uint8_t p2, uint8_t p3,
                                const uint8_t *tx, size_t tx_len,
                                uint8_t *rx, size_t rx_cap, size_t *rx_len,
                                uint16_t *sw)
{
    const uint8_t hdr[ISO7816_T0_HEADER_LEN] = { cla, ins, p1, p2, p3 };
    for (int i = 0; i < (int)ISO7816_T0_HEADER_LEN; i++) {
        iso7816_status_t stat = iso7816_char_tx(dev, hdr[i]);
        if (stat != ISO7816_OK) return stat;
    }

    size_t sent = 0;
    size_t recvd = 0;
    size_t expected_rx = p3;    /* expected outgoing data bytes */

    for (;;) {
        uint8_t proc_byte;
        iso7816_status_t stat = t0_rx(dev, &proc_byte);
        if (stat != ISO7816_OK) return stat;

        if (proc_byte == ISO7816_T0_NULL) continue;     /* NULL */

        if (is_sw1(proc_byte)) {                        /* SW1 -> attendre SW2 */
            uint8_t sw2;
            stat = t0_rx(dev, &sw2);
            if (stat != ISO7816_OK) return stat;
            *sw = (uint16_t)((proc_byte << 8) | sw2);
            *rx_len = recvd;
            return ISO7816_OK;
        }

        if (proc_byte == ins || proc_byte == (uint8_t)(ins ^ ISO7816_T0_ACK_INVERT_MASK)) { /* ACK */
            if (sent < tx_len) {
                /* incoming command: send the remaining data (or 1 byte) */
                if (proc_byte == ins) {
                    for (size_t i = sent; i < tx_len; i++) {
                        stat = iso7816_char_tx(dev, tx[i]);
                        if (stat != ISO7816_OK) return stat;
                    }
                    sent = tx_len;
                } else {
                    stat = iso7816_char_tx(dev, tx[sent++]);
                    if (stat != ISO7816_OK) return stat;
                }
                continue;
            }
            /* outgoing command: receive data until SW1-SW2
               (SW1 '6X'/'9X' ends the data — case '61xx', cl. 12.2) */
            for (;;) {
                if (recvd >= expected_rx && expected_rx > 0) {
                    /* P3 data bytes received: SW1-SW2 follow */
                    uint8_t sw1, sw2;
                    stat = t0_rx(dev, &sw1);
                    if (stat != ISO7816_OK) return stat;
                    stat = t0_rx(dev, &sw2);
                    if (stat != ISO7816_OK) return stat;
                    *sw = (uint16_t)((sw1 << 8) | sw2);
                    *rx_len = recvd;
                    return ISO7816_OK;
                }
                uint8_t byte;
                stat = t0_rx(dev, &byte);
                if (stat != ISO7816_OK) return stat;
                if (is_sw1(byte)) {
                    uint8_t sw2;
                    stat = t0_rx(dev, &sw2);
                    if (stat != ISO7816_OK) return stat;
                    *sw = (uint16_t)((byte << 8) | sw2);
                    *rx_len = recvd;
                    return ISO7816_OK;
                }
                if (recvd >= rx_cap) return ISO7816_GEN_SIZE;
                rx[recvd++] = byte;
                if (proc_byte == (uint8_t)(ins ^ ISO7816_T0_ACK_INVERT_MASK)) break; /* partial ACK: 1 byte only */
            }
            continue;
        }

        return ISO7816_T0_PROC;   /* invalid procedure byte (cl. 10.3.3) */
    }
}

iso7816_status_t iso7816_t0_exchange(iso7816_t *dev,
                                     const uint8_t *cmd, size_t cmd_len,
                                     uint8_t *resp, size_t resp_cap,
                                     size_t *resp_len, uint16_t *sw)
{
    if (!dev || !dev->active || dev->protocol != 0) return ISO7816_GEN_STATE;

    iso7816_apdu_cmd_t apdu;
    iso7816_status_t stat = iso7816_apdu_decode(cmd, cmd_len, &apdu);
    if (stat != ISO7816_OK) return stat;

    /* extended fields not handled by T=0 (ENVELOPE required, out of scope) */
    if (apdu.apdu_case == ISO7816_APDU_CASE_2E || apdu.apdu_case == ISO7816_APDU_CASE_3E ||
        apdu.apdu_case == ISO7816_APDU_CASE_4E) {
        return ISO7816_APDU_INVALID;
    }

    bool incoming = (apdu.apdu_case == ISO7816_APDU_CASE_3S || apdu.apdu_case == ISO7816_APDU_CASE_4S);
    bool outgoing = (apdu.apdu_case == ISO7816_APDU_CASE_2S || apdu.apdu_case == ISO7816_APDU_CASE_4S);

    uint8_t p3;
    if (incoming) {
        p3 = (uint8_t)apdu.nc;      /* cases 3S/4S: nc <= 255 */
    } else if (outgoing) {
        p3 = (apdu.ne >= ISO7816_T0_NE_256) ? 0 : (uint8_t)apdu.ne;
    } else {
        p3 = 0;
    }

    size_t total_rx = 0;
    uint16_t cur_sw = 0;

    for (unsigned retransmit_count = 0; retransmit_count < ISO7816_T0_MAX_6C_RETRIES; retransmit_count++) {
        size_t part_len = 0;
        stat = t0_tpdu(dev, apdu.cla, apdu.ins, apdu.p1, apdu.p2, p3,
                       incoming ? apdu.data : NULL, incoming ? apdu.nc : 0,
                       resp + total_rx, resp_cap - total_rx, &part_len, &cur_sw);
        if (stat != ISO7816_OK) return stat;
        total_rx += part_len;

        if (cur_sw == ISO7816_SW_OK ||
            (cur_sw & ISO7816_SW1_MASK) == (ISO7816_SW1_GET_RESPONSE << 8) ||
            (cur_sw & ISO7816_SW1_MASK) == (ISO7816_SW1_RETRANSMIT << 8)) {
            /* '6Cxx': retransmit the same command with P3 = SW2 (cl. 12.2, table 14) */
            if ((cur_sw & ISO7816_SW1_MASK) == (ISO7816_SW1_RETRANSMIT << 8)) {
                p3 = cur_sw & ISO7816_SW2_MASK;
                continue;
            }
            break;
        }
        break;
    }

    /* '61xx': remaining data via GET RESPONSE (cl. 12.2) */
    while ((cur_sw & ISO7816_SW1_MASK) == (ISO7816_SW1_GET_RESPONSE << 8)) {
        uint8_t more = cur_sw & ISO7816_SW2_MASK;       /* 0 = 256 */
        uint32_t want = apdu.ne ? apdu.ne : ISO7816_T0_NE_256;
        uint32_t remaining = (total_rx < want) ? (uint32_t)(want - total_rx) : 0;
        uint8_t p3g = (uint8_t)((remaining == 0) ? 0
                                : (more == 0 ? 0 : (more < remaining ? more : remaining)));
        if (p3g == 0 && more != 0) p3g = 0;              /* '00' = 256 */
        size_t part_len = 0;
        stat = t0_tpdu(dev, ISO7816_GET_RESPONSE_CLA, ISO7816_INS_GET_RESPONSE, 0x00, 0x00, p3g,
                       NULL, 0, resp + total_rx, resp_cap - total_rx, &part_len, &cur_sw);
        if (stat != ISO7816_OK) return stat;
        total_rx += part_len;
        if ((cur_sw & ISO7816_SW1_MASK) != (ISO7816_SW1_GET_RESPONSE << 8) && cur_sw != ISO7816_SW_OK) break;
        if (part_len == 0) break;
    }

    *resp_len = total_rx;
    *sw = cur_sw;
    return ISO7816_OK;
}
