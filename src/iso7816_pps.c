#include "iso7816/iso7816_pps.h"

#include "iso7816/iso7816_char.h"
#include "iso7816/iso7816_types.h"
#include "iso7816_internal.h"

/* Applies the selected protocol parameters (declared in iso7816.c). */
void iso7816_apply_protocol(iso7816_t *dev);

static iso7816_status_t pps_rx_byte(iso7816_t *dev, uint8_t *byte, uint32_t wt_us)
{
    for (uint8_t attempt = 0; attempt <= dev->max_retries; attempt++) {
        iso7816_status_t stat = iso7816_char_rx(dev, byte, wt_us);
        if (stat == ISO7816_OK) return ISO7816_OK;
        if (stat == ISO7816_CHAR_WT) return stat;
        /* PARITY: the error signal was sent, retry */
    }
    return ISO7816_CHAR_PARITY;
}

iso7816_status_t iso7816_pps_exchange(iso7816_t *dev, uint8_t protocol,
                                      uint16_t F, uint8_t D)
{
    if (!dev || !dev->active) return ISO7816_GEN_STATE;

    /* the proposed protocol must be offered by the card (cl. 9 + 8.2.3) */
    bool offered = (dev->atr.n_protocols == 0 && protocol == 0);
    for (uint8_t i = 0; i < dev->atr.n_protocols; i++) {
        if (dev->atr.protocols[i] == protocol) offered = true;
    }
    if (!offered) return ISO7816_PPS_PROTOCOL;

    /* F/D within [Fd..Fi] x [Dd..Di] (cl. 9.2) */
    if (F < ISO7816_F_DEFAULT || F > dev->atr.fi ||
        D < ISO7816_D_DEFAULT || D > dev->atr.di) {
        return ISO7816_GEN_PARAM;
    }

    bool found;
    uint8_t fi_code = iso7816_fi_to_code(F, dev->atr.fmax_mhz, &found);
    if (!found) return ISO7816_GEN_PARAM;
    uint8_t di_code = iso7816_di_to_code(D, &found);
    if (!found) return ISO7816_GEN_PARAM;

    /* PPS request : PPSS, PPS0, PPS1, PCK (cl. 9.2) */
    uint8_t req[ISO7816_PPS_REQ_LEN];
    req[0] = ISO7816_PPS_PPSS;                                /* PPSS */
    req[1] = (uint8_t)((protocol & ISO7816_PPS0_PROTO_MASK) | ISO7816_PPS0_PPS1_BIT);
    req[2] = (uint8_t)((fi_code << ISO7816_TA1_FI_SHIFT) | di_code); /* PPS1 : comme TA1 */
    uint8_t pck = 0;
    for (int i = 0; i < (int)ISO7816_PPS_REQ_LEN - 1; i++) pck ^= req[i];
    req[3] = pck;                                             /* PCK */

    const uint32_t wt_us = ISO7816_WT_ATR_PPS_ETU * iso7816_etu_us(dev); /* WT = 9600 etu (cl. 9.1) */

    for (int i = 0; i < (int)ISO7816_PPS_REQ_LEN; i++) {
        iso7816_status_t stat = iso7816_char_tx(dev, req[i]);
        if (stat != ISO7816_OK) return stat;
    }

    /* PPS response */
    uint8_t resp[ISO7816_PPS_RESP_MAX];
    size_t resp_len = 0;
    for (;;) {
        if (resp_len >= sizeof(resp)) return ISO7816_PPS_FAILED;
        iso7816_status_t stat = pps_rx_byte(dev, &resp[resp_len], wt_us);
        if (stat != ISO7816_OK) return ISO7816_PPS_FAILED;
        resp_len++;
        if (resp_len == 1) {
            if (resp[0] != ISO7816_PPS_PPSS) return ISO7816_PPS_FAILED;
            continue;
        }
        if (resp_len == 2) continue;   /* PPS0 received, continue according to its bits */
        uint8_t pps0 = resp[1];
        size_t expected = 2;
        if (pps0 & ISO7816_PPS0_PPS1_BIT) expected++;
        if (pps0 & ISO7816_PPS0_PPS2_BIT) expected++;
        if (pps0 & ISO7816_PPS0_PPS3_BIT) expected++;
        expected++;                    /* PCK */
        if (resp_len >= expected) break;
    }

    /* PCK: XOR of PPSS..PCK == 0 (cl. 9.2) */
    uint8_t xor_acc = 0;
    for (size_t i = 0; i < resp_len; i++) xor_acc ^= resp[i];
    if (xor_acc != 0) return ISO7816_PPS_FAILED;

    /* success: PPS0 bits 1..4 identical, bit 5 -> PPS1 identical or absent (cl. 9.3) */
    if ((resp[1] & ISO7816_PPS0_PROTO_MASK) != (req[1] & ISO7816_PPS0_PROTO_MASK)) {
        return ISO7816_PPS_FAILED;
    }
    bool f_d_accepted = false;
    if (resp[1] & ISO7816_PPS0_PPS1_BIT) {
        if (resp_len < ISO7816_PPS_REQ_LEN || resp[2] != req[2]) return ISO7816_PPS_FAILED;
        f_d_accepted = true;
    }

    if (f_d_accepted) {
        dev->F = F;
        dev->D = D;
    } else {
        dev->F = ISO7816_F_DEFAULT;     /* Fd */
        dev->D = ISO7816_D_DEFAULT;     /* Dd */
    }
    dev->protocol = protocol;
    iso7816_apply_protocol(dev);
    return ISO7816_OK;
}
