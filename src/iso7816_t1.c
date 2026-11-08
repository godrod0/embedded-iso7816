#include "iso7816/iso7816_t1.h"

#include <string.h>

#include "iso7816/iso7816_apdu.h"
#include "iso7816/iso7816_char.h"
#include "iso7816_internal.h"

static uint32_t t1_cwt_us(const iso7816_t *dev)
{
    /* CWT = (11 + 2^CWI) etu (cl. 11.4.3) */
    return (ISO7816_T1_CWT_BASE_ETU + (1U << dev->cwi)) * iso7816_etu_us(dev);
}

static uint32_t t1_bwt_us(const iso7816_t *dev)
{
    /* BWT = 11 etu + 2^BWI * 960 * Fd/f, Fd = 372 (cl. 11.4.3) */
    uint32_t etu = iso7816_etu_us(dev);
    uint64_t extra = ((uint64_t)(1U << dev->bwi) * ISO7816_T1_BWT_ETU_PER_BWI *
                      ISO7816_T1_BWT_FD * ISO7816_US_PER_SECOND) / dev->f_hz;
    return ISO7816_T1_BWT_BASE_ETU * etu + (uint32_t)extra;
}

/* CRC-16 selon ISO/IEC 13239 (poly 0x1021, init 0xFFFF) — cl. 11.3.4 */
static uint16_t t1_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = ISO7816_T1_CRC16_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (int bit_index = 0; bit_index < (int)ISO7816_BITS_PER_BYTE; bit_index++) {
            crc = (crc & ISO7816_T1_CRC16_MSB_MASK)
                  ? (uint16_t)((crc << 1) ^ ISO7816_T1_CRC16_POLY)
                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static bool t1_is_iblock(uint8_t pcb) { return (pcb & ISO7816_T1_PCB_IBLOCK_BIT) == 0; }
static bool t1_is_rblock(uint8_t pcb) { return (pcb & ISO7816_T1_PCB_TYPE_MASK) == ISO7816_T1_PCB_RBLOCK; }
static bool t1_is_sblock(uint8_t pcb) { return (pcb & ISO7816_T1_PCB_TYPE_MASK) == ISO7816_T1_PCB_SBLOCK; }

static iso7816_status_t t1_send_block(iso7816_t *dev, uint8_t nad, uint8_t pcb,
                                       const uint8_t *inf, uint8_t len)
{
    if (dev->last_dir_rx) {
        /* BGT entre le dernier caractère reçu et le premier émis (cl. 11.2) */
        iso7816_wait_until(dev, dev->last_rx_lead_us + ISO7816_T1_BGT_ETU * iso7816_etu_us(dev));
        dev->last_dir_rx = false;
    }

    uint8_t buf[ISO7816_T1_HEADER_LEN + ISO7816_T1_INF_MAX];
    buf[0] = nad;
    buf[1] = pcb;
    buf[2] = len;
    if (len > 0) memcpy(buf + ISO7816_T1_HEADER_LEN, inf, len);
    size_t block_len = ISO7816_T1_HEADER_LEN + len;

    for (size_t i = 0; i < block_len; i++) {
        iso7816_status_t stat = iso7816_char_tx(dev, buf[i]);
        if (stat != ISO7816_OK) return stat;
    }

    if (dev->use_crc) {
        uint16_t crc_val = t1_crc16(buf, block_len);
        iso7816_status_t stat = iso7816_char_tx(dev, (uint8_t)(crc_val >> 8));
        if (stat != ISO7816_OK) return stat;
        stat = iso7816_char_tx(dev, (uint8_t)(crc_val & ISO7816_BYTE_MASK));
        if (stat != ISO7816_OK) return stat;
    } else {
        uint8_t lrc = 0;
        for (size_t i = 0; i < block_len; i++) lrc ^= buf[i];
        iso7816_status_t stat = iso7816_char_tx(dev, lrc);
        if (stat != ISO7816_OK) return stat;
    }
    return ISO7816_OK;
}

static iso7816_status_t t1_recv_byte(iso7816_t *dev, uint8_t *byte, uint32_t timeout_us)
{
    return iso7816_char_rx(dev, byte, timeout_us);
}

/*
 * Reçoit un bloc. timeout_us s'applique au premier caractère (BWT).
 * Retourne ISO7816_CHAR_WT si la carte est muette, ISO7816_T1_EDC si le
 * bloc est invalide (parité, LEN, LRC/CRC — cl. 11.6.3.1).
 */
static iso7816_status_t t1_recv_block(iso7816_t *dev, iso7816_t1_block_t *block,
                                       uint32_t timeout_us)
{
    uint8_t byte;
    iso7816_status_t stat = t1_recv_byte(dev, &byte, timeout_us);
    if (stat != ISO7816_OK) return stat;       /* CHAR_WT = BWT dépassé */
    block->nad = byte;
    dev->last_rx_lead_us = iso7816_now(dev);
    dev->last_dir_rx = true;

    uint32_t cwt_us = t1_cwt_us(dev);
    stat = t1_recv_byte(dev, &byte, cwt_us);
    if (stat != ISO7816_OK) return ISO7816_T1_EDC;
    block->pcb = byte;
    stat = t1_recv_byte(dev, &byte, cwt_us);
    if (stat != ISO7816_OK) return ISO7816_T1_EDC;
    block->len = byte;
    block->inf_len = byte;
    if (byte > ISO7816_T1_INF_MAX) return ISO7816_T1_EDC;

    uint8_t lrc_acc = block->nad ^ block->pcb ^ block->len;
    for (uint8_t i = 0; i < byte; i++) {
        stat = t1_recv_byte(dev, &block->inf[i], cwt_us);
        if (stat != ISO7816_OK) return ISO7816_T1_EDC;
        lrc_acc ^= block->inf[i];
    }

    if (dev->use_crc) {
        uint8_t crc_hi, crc_lo;
        stat = t1_recv_byte(dev, &crc_hi, cwt_us);
        if (stat != ISO7816_OK) return ISO7816_T1_EDC;
        stat = t1_recv_byte(dev, &crc_lo, cwt_us);
        if (stat != ISO7816_OK) return ISO7816_T1_EDC;
        uint8_t crc_buf[ISO7816_T1_HEADER_LEN + ISO7816_T1_INF_MAX];
        crc_buf[0] = block->nad; crc_buf[1] = block->pcb; crc_buf[2] = block->len;
        memcpy(crc_buf + ISO7816_T1_HEADER_LEN, block->inf, byte);
        uint16_t crc_val = t1_crc16(crc_buf, ISO7816_T1_HEADER_LEN + byte);
        if (crc_val != (uint16_t)((crc_hi << 8) | crc_lo)) return ISO7816_T1_EDC;
    } else {
        uint8_t lrc;
        stat = t1_recv_byte(dev, &lrc, cwt_us);
        if (stat != ISO7816_OK) return ISO7816_T1_EDC;
        if ((uint8_t)(lrc_acc ^ lrc) != 0) return ISO7816_T1_EDC;
    }
    return ISO7816_OK;
}

static iso7816_status_t t1_send_r(iso7816_t *dev, uint8_t nr, uint8_t err_code)
{
    uint8_t pcb = (uint8_t)(ISO7816_T1_PCB_RBLOCK |
                            ((nr & 1U) != 0 ? ISO7816_T1_PCB_NR_BIT : 0) |
                            (err_code & ISO7816_T1_PCB_FUNC_MASK));
    return t1_send_block(dev, ISO7816_T1_NAD_DEFAULT, pcb, NULL, 0);
}

static iso7816_status_t t1_send_s(iso7816_t *dev, uint8_t s_func, bool is_response,
                                  const uint8_t *inf, uint8_t len)
{
    uint8_t pcb = (uint8_t)(ISO7816_T1_PCB_SBLOCK |
                            (is_response ? ISO7816_T1_PCB_RESP_BIT : 0) |
                            (s_func & ISO7816_T1_PCB_FUNC_MASK));
    return t1_send_block(dev, ISO7816_T1_NAD_DEFAULT, pcb, inf, len);
}

/* RESYNCH : max 3 tentatives (règle 6.4, cl. 11.6.3.2) */
static iso7816_status_t t1_resynch(iso7816_t *dev)
{
    for (int attempt = 0; attempt < (int)ISO7816_T1_RESYNC_MAX; attempt++) {
        iso7816_status_t stat = t1_send_s(dev, ISO7816_T1_S_RESYNCH, false, NULL, 0);
        if (stat != ISO7816_OK) return stat;
        iso7816_t1_block_t block;
        stat = t1_recv_block(dev, &block, t1_bwt_us(dev));
        if (stat == ISO7816_OK && t1_is_sblock(block.pcb) &&
            (block.pcb & ISO7816_T1_PCB_FUNC_MASK) == ISO7816_T1_S_RESYNCH &&
            (block.pcb & ISO7816_T1_PCB_RESP_BIT)) {
            dev->ns_ifd = 0;
            dev->ns_card = 0;
            return ISO7816_OK;
        }
    }
    return ISO7816_T1_RESYNC;
}

/* Envoie la commande APDU en I-blocs chaînés si nécessaire (bit M, cl. 11.6.2.2). */
static iso7816_status_t t1_send_apdu(iso7816_t *dev, const uint8_t *cmd, size_t cmd_len)
{
    size_t offset = 0;
    do {
        size_t chunk = cmd_len - offset;
        if (chunk > dev->ifsc) chunk = dev->ifsc;
        bool more = (offset + chunk) < cmd_len;
        uint8_t pcb = (uint8_t)(((dev->ns_ifd & 1U) != 0 ? ISO7816_T1_PCB_NS_BIT : 0) |
                                (more ? ISO7816_T1_PCB_MORE_BIT : 0));
        iso7816_status_t stat = t1_send_block(dev, ISO7816_T1_NAD_DEFAULT, pcb,
                                               cmd + offset, (uint8_t)chunk);
        if (stat != ISO7816_OK) return stat;
        dev->ns_ifd ^= 1;
        offset += chunk;
        if (more) {
            /* acquittement R(N(R) = N(S) attendu) — règle 2.2 */
            iso7816_t1_block_t block;
            stat = t1_recv_block(dev, &block, t1_bwt_us(dev));
            if (stat != ISO7816_OK) return stat;
            if (!t1_is_rblock(block.pcb) ||
                ((block.pcb & ISO7816_T1_PCB_NR_BIT) != 0) != (dev->ns_ifd != 0) ||
                (block.pcb & ISO7816_T1_PCB_FUNC_MASK) != 0) {
                return ISO7816_T1_SEQ;
            }
        }
    } while (offset < cmd_len);
    return ISO7816_OK;
}

/* Reçoit la réponse (I-blocs jusqu'à M=0). Données accumulées dans resp. */
static iso7816_status_t t1_recv_response(iso7816_t *dev,
                                         uint8_t *resp, size_t cap, size_t *recvd,
                                         uint16_t *sw)
{
    size_t total_len = 0;
    uint8_t expected_ns = dev->ns_card;
    uint32_t bwt_us = t1_bwt_us(dev);
    int retries = 0;

    for (;;) {
        iso7816_t1_block_t block;
        iso7816_status_t stat = t1_recv_block(dev, &block, bwt_us);

        if (stat == ISO7816_CHAR_WT) {
            /* BWT dépassé : R(N(R)) puis max 2 retries, sinon RESYNCH (règles 7.1/7.4) */
            if (++retries > (int)ISO7816_T1_MAX_RETRIES) {
                iso7816_status_t stat_resynch = t1_resynch(dev);
                return (stat_resynch == ISO7816_OK) ? ISO7816_T1_BWT : stat_resynch;
            }
            stat = t1_send_r(dev, expected_ns, 0);
            if (stat != ISO7816_OK) return stat;
            continue;
        }
        if (stat != ISO7816_OK) {
            /* bloc invalide : R avec code erreur EDC, max 2 retries (règle 7.1/7.4.2) */
            if (++retries > (int)ISO7816_T1_MAX_RETRIES) {
                iso7816_status_t stat_resynch = t1_resynch(dev);
                return (stat_resynch == ISO7816_OK) ? ISO7816_T1_EDC : stat_resynch;
            }
            stat = t1_send_r(dev, expected_ns, ISO7816_T1_R_ERR_EDC);
            if (stat != ISO7816_OK) return stat;
            continue;
        }
        retries = 0;

        if (t1_is_sblock(block.pcb)) {
            uint8_t s_func = block.pcb & ISO7816_T1_PCB_FUNC_MASK;
            bool is_response = (block.pcb & ISO7816_T1_PCB_RESP_BIT) != 0;
            if (s_func == ISO7816_T1_S_WTX && !is_response) {
                /* S(WTX request) : acquitter et étendre BWT (règle 3) */
                iso7816_status_t stat_resp = t1_send_s(dev, ISO7816_T1_S_WTX, true,
                                                        block.inf, block.inf_len);
                if (stat_resp != ISO7816_OK) return stat_resp;
                if (block.inf_len >= 1) {
                    bwt_us = t1_bwt_us(dev) * block.inf[0];
                }
                continue;
            }
            if (s_func == ISO7816_T1_S_IFS && !is_response) {
                iso7816_status_t stat_resp = t1_send_s(dev, ISO7816_T1_S_IFS, true,
                                                        block.inf, block.inf_len);
                if (stat_resp != ISO7816_OK) return stat_resp;
                if (block.inf_len >= 1) dev->ifsc = block.inf[0];
                continue;
            }
            if (s_func == ISO7816_T1_S_ABORT && !is_response) {
                iso7816_status_t stat_resp = t1_send_s(dev, ISO7816_T1_S_ABORT, true, NULL, 0);
                if (stat_resp != ISO7816_OK) return stat_resp;
                return ISO7816_T1_SEQ;
            }
            continue;
        }

        if (t1_is_rblock(block.pcb)) {
            /* acquittement (ou erreur) de notre dernier bloc : on continue */
            if ((block.pcb & ISO7816_T1_PCB_FUNC_MASK) != 0) return ISO7816_T1_EDC;
            continue;
        }

        if (t1_is_iblock(block.pcb)) {
            bool ns_received = (block.pcb & ISO7816_T1_PCB_NS_BIT) != 0;
            if (ns_received != (expected_ns != 0)) {
                /* erreur de séquence : R(N(R)=attendu) code OTHER (cl. 11.3.2.2) */
                if (++retries > (int)ISO7816_T1_MAX_RETRIES) return ISO7816_T1_SEQ;
                iso7816_status_t stat_resp = t1_send_r(dev, expected_ns, ISO7816_T1_R_ERR_OTHER);
                if (stat_resp != ISO7816_OK) return stat_resp;
                continue;
            }
            if (total_len + block.inf_len > cap) return ISO7816_GEN_SIZE;
            memcpy(resp + total_len, block.inf, block.inf_len);
            total_len += block.inf_len;
            expected_ns ^= 1;
            dev->ns_card = expected_ns;
            bool more = (block.pcb & ISO7816_T1_PCB_MORE_BIT) != 0;
            /* acquittement du bloc reçu (règles 2.1/2.2) */
            iso7816_status_t stat_resp = t1_send_r(dev, expected_ns, 0);
            if (stat_resp != ISO7816_OK) return stat_resp;
            if (!more) break;
        }
    }

    if (total_len < ISO7816_SW_LEN) return ISO7816_T1_SEQ;
    *sw = (uint16_t)((resp[total_len - ISO7816_SW_LEN] << 8) | resp[total_len - 1]);
    *recvd = total_len - ISO7816_SW_LEN;
    return ISO7816_OK;
}

iso7816_status_t iso7816_t1_exchange(iso7816_t *dev,
                                     const uint8_t *cmd, size_t cmd_len,
                                     uint8_t *resp, size_t resp_cap,
                                     size_t *resp_len, uint16_t *sw)
{
    if (!dev || !dev->active || dev->protocol != 1) return ISO7816_GEN_STATE;

    iso7816_status_t stat = t1_send_apdu(dev, cmd, cmd_len);
    if (stat != ISO7816_OK) return stat;

    size_t recvd = 0;
    uint16_t cur_sw = 0;
    stat = t1_recv_response(dev, resp, resp_cap, &recvd, &cur_sw);
    if (stat != ISO7816_OK) return stat;

    /* '61xx' : GET RESPONSE par T=1 (les données suivent en I-blocs chaînés) */
    unsigned gr_count = 0;
    while ((cur_sw & ISO7816_SW1_MASK) == (ISO7816_SW1_GET_RESPONSE << 8) &&
           gr_count++ < ISO7816_T1_MAX_GET_RESPONSE) {
        uint8_t more = (uint8_t)(cur_sw & ISO7816_SW2_MASK);   /* 0 = 256 */
        uint8_t p3 = more;                                      /* P3 = octets restants demandés */
        uint8_t get_resp[ISO7816_T0_HEADER_LEN] =
            { ISO7816_GET_RESPONSE_CLA, ISO7816_INS_GET_RESPONSE, 0x00, 0x00, p3 };
        stat = t1_send_apdu(dev, get_resp, sizeof(get_resp));
        if (stat != ISO7816_OK) return stat;
        size_t part_len = 0;
        stat = t1_recv_response(dev, resp + recvd, resp_cap - recvd, &part_len, &cur_sw);
        if (stat != ISO7816_OK) return stat;
        recvd += part_len;
    }

    *resp_len = recvd;
    *sw = cur_sw;
    return ISO7816_OK;
}
