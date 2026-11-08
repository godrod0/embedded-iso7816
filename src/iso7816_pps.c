#include "iso7816/iso7816_pps.h"

#include "iso7816/iso7816_char.h"
#include "iso7816/iso7816_types.h"
#include "iso7816_internal.h"

/* Applique les paramètres du protocole retenu (déclaré dans iso7816.c). */
void iso7816_apply_protocol(iso7816_t *d);

static iso7816_status_t pps_rx_byte(iso7816_t *d, uint8_t *b, uint32_t wt_us)
{
    for (uint8_t attempt = 0; attempt <= d->max_retries; attempt++) {
        iso7816_status_t st = iso7816_char_rx(d, b, wt_us);
        if (st == ISO7816_OK) return ISO7816_OK;
        if (st == ISO7816_CHAR_WT) return st;
        /* PARITY : le signal d'erreur a été émis, on retente */
    }
    return ISO7816_CHAR_PARITY;
}

iso7816_status_t iso7816_pps_exchange(iso7816_t *d, uint8_t protocol,
                                      uint16_t F, uint8_t D)
{
    if (!d || !d->active) return ISO7816_GEN_STATE;

    /* le protocole proposé doit être offert par la carte (cl. 9 + 8.2.3) */
    bool offered = (d->atr.n_protocols == 0 && protocol == 0);
    for (uint8_t i = 0; i < d->atr.n_protocols; i++) {
        if (d->atr.protocols[i] == protocol) offered = true;
    }
    if (!offered) return ISO7816_PPS_PROTOCOL;

    /* F/D dans [Fd..Fi] x [Dd..Di] (cl. 9.2) */
    if (F < 372 || F > d->atr.fi || D < 1 || D > d->atr.di) {
        return ISO7816_GEN_PARAM;
    }

    bool found;
    uint8_t ficode = iso7816_fi_to_code(F, d->atr.fmax_mhz, &found);
    if (!found) return ISO7816_GEN_PARAM;
    uint8_t dicode = iso7816_di_to_code(D, &found);
    if (!found) return ISO7816_GEN_PARAM;

    /* PPS request : PPSS, PPS0, PPS1, PCK (cl. 9.2) */
    uint8_t req[4];
    req[0] = 0xFF;                              /* PPSS */
    req[1] = (uint8_t)((protocol & 0x0F) | 0x10); /* PPS1 présent, type T */
    req[2] = (uint8_t)((ficode << 4) | dicode);   /* PPS1 : comme TA1 */
    uint8_t pck = 0;
    for (int i = 0; i < 3; i++) pck ^= req[i];
    req[3] = pck;                               /* PCK */

    const uint32_t wt = 9600U * iso7816_etu_us(d);   /* WT = 9600 etu (cl. 9.1) */

    for (int i = 0; i < 4; i++) {
        iso7816_status_t st = iso7816_char_tx(d, req[i]);
        if (st != ISO7816_OK) return st;
    }

    /* PPS response */
    uint8_t resp[6];
    size_t n = 0;
    for (;;) {
        if (n >= sizeof(resp)) return ISO7816_PPS_FAILED;
        iso7816_status_t st = pps_rx_byte(d, &resp[n], wt);
        if (st != ISO7816_OK) return ISO7816_PPS_FAILED;
        n++;
        if (n == 1) {
            if (resp[0] != 0xFF) return ISO7816_PPS_FAILED;
            continue;
        }
        if (n == 2) continue;   /* PPS0 reçu, on continue selon ses bits */
        uint8_t pps0 = resp[1];
        size_t expected = 2;
        if (pps0 & 0x10) expected++;
        if (pps0 & 0x20) expected++;
        if (pps0 & 0x40) expected++;
        expected++;             /* PCK */
        if (n >= expected) break;
    }

    /* PCK : XOR de PPSS..PCK == 0 (cl. 9.2) */
    uint8_t x = 0;
    for (size_t i = 0; i < n; i++) x ^= resp[i];
    if (x != 0) return ISO7816_PPS_FAILED;

    /* succès : bits 1..4 de PPS0 identiques, bit 5 -> PPS1 identique ou absent (cl. 9.3) */
    if ((resp[1] & 0x0F) != (req[1] & 0x0F)) return ISO7816_PPS_FAILED;
    bool f_d_accepted = false;
    if (resp[1] & 0x10) {
        if (n < 4 || resp[2] != req[2]) return ISO7816_PPS_FAILED;
        f_d_accepted = true;
    }

    if (f_d_accepted) {
        d->F = F;
        d->D = D;
    } else {
        d->F = 372;     /* Fd */
        d->D = 1;       /* Dd */
    }
    d->protocol = protocol;
    iso7816_apply_protocol(d);
    return ISO7816_OK;
}