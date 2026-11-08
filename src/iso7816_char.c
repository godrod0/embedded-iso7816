#include "iso7816/iso7816_char.h"

#include "iso7816_internal.h"

uint32_t iso7816_etu_us(const iso7816_t *d)
{
    return (uint32_t)(((uint64_t)d->F * 1000000ULL) / ((uint64_t)d->D * d->f_hz));
}

uint32_t iso7816_gt_us(const iso7816_t *d)
{
    uint32_t etu = iso7816_etu_us(d);
    uint32_t gt = 12U * etu;
    if (d->guard_n == 0 || d->guard_n == 255) {
        /* N=255 : GT = 12 etu en PPS et T=0 (cl. 8.3) */
        return gt;
    }
    /* GT = 12 etu + R*N/f ; R = F/D, ou Fi/Di si T=15 présent (cl. 8.3) */
    uint64_t extra;
    if (d->has_t15) {
        extra = ((uint64_t)d->atr.fi * d->guard_n * 1000000ULL) /
                ((uint64_t)d->atr.di * d->f_hz);
    } else {
        extra = (uint64_t)d->guard_n * etu;
    }
    return gt + (uint32_t)extra;
}

static bool bit_level(const iso7816_t *d, bool value)
{
    /* direct ('3B') : H encode 1 ; inverse ('3F') : L encode 1 */
    return d->inverse ? !value : value;
}

iso7816_status_t iso7816_char_tx(iso7816_t *d, uint8_t b)
{
    const uint32_t etu = iso7816_etu_us(d);
    const uint32_t gt = iso7816_gt_us(d);

    for (uint8_t attempt = 0; attempt <= d->max_retries; attempt++) {
        uint32_t t0 = iso7816_now(d);
        d->last_tx_lead_us = t0;

        d->hal->io_tx(d->hal->ctx, false);          /* moment 1 : start L */

        for (int m = 2; m <= 9; m++) {              /* moments 2..9 : données */
            int bit = d->inverse ? (9 - m) : (m - 2); /* inverse: msb d'abord ; direct: lsb d'abord */
            bool v = ((b >> bit) & 1U) != 0;
            iso7816_wait_until(d, t0 + (uint32_t)(m - 1) * etu);
            d->hal->io_tx(d->hal->ctx, bit_level(d, v));
        }

        /* moment 10 : parité paire sur les 9 moments 2..10 (cl. 8.1) */
        bool parity = (__builtin_popcount(b) & 1U) != 0;
        iso7816_wait_until(d, t0 + 9U * etu);
        d->hal->io_tx(d->hal->ctx, bit_level(d, parity));

        iso7816_wait_until(d, t0 + 10U * etu);
        d->hal->io_rx(d->hal->ctx);                 /* libération de la ligne */

        /* détection du signal d'erreur à (11 ± 0,2) etu (cl. 7.3) */
        iso7816_wait_until(d, t0 + 11U * etu);
        if (!d->hal->io_read(d->hal->ctx)) {
            /* erreur : attendre >= 2 etu après détection, puis répéter */
            iso7816_wait_until(d, t0 + 13U * etu);
            continue;
        }

        /* guard time entre leading edges de caractères consécutifs (cl. 7.2) */
        iso7816_wait_until(d, t0 + gt);
        d->last_dir_rx = false;
        return ISO7816_OK;
    }
    return ISO7816_CHAR_PARITY;
}

iso7816_status_t iso7816_char_rx(iso7816_t *d, uint8_t *b, uint32_t timeout_us)
{
    const uint32_t etu = iso7816_etu_us(d);
    const uint32_t start = iso7816_now(d);

    /* attente du front start (passage à L), confirmé avant 0,7 etu (cl. 7.2) */
    uint32_t t0 = 0;
    for (;;) {
        if (!d->hal->io_read(d->hal->ctx)) {
            t0 = iso7816_now(d);
            iso7816_wait_until(d, t0 + etu / 2);
            if (!d->hal->io_read(d->hal->ctx)) {
                break;   /* start confirmé : toujours L à 0,5 etu */
            }
        }
        if ((int32_t)(iso7816_now(d) - start) >= (int32_t)timeout_us) {
            return ISO7816_CHAR_WT;
        }
    }

    uint8_t v = 0;
    unsigned ones = 0;
    for (int m = 2; m <= 10; m++) {
        iso7816_wait_until(d, t0 + (uint32_t)((2 * m - 1) * etu / 2)); /* (m-0,5)±0,2 etu */
        bool level = d->hal->io_read(d->hal->ctx);
        bool bit = d->inverse ? !level : level;
        ones += bit ? 1U : 0U;
        if (m <= 9) {
            int idx = d->inverse ? (9 - m) : (m - 2);
            if (bit) {
                v |= (uint8_t)(1U << idx);
            }
        }
    }

    if ((ones & 1U) != 0U) {
        /* parité incorrecte : signal d'erreur à (10,5±0,2) etu, 1..2 etu (cl. 7.3) */
        if (d->use_error_signal) {
            iso7816_wait_until(d, t0 + (uint32_t)(21 * etu / 2)); /* 10,5 etu */
            d->hal->io_tx(d->hal->ctx, false);
            iso7816_wait_until(d, t0 + 12U * etu);                 /* impulsion ~1,5 etu */
            d->hal->io_rx(d->hal->ctx);
        }
        return ISO7816_CHAR_PARITY;
    }

    *b = v;
    d->last_rx_lead_us = t0;
    d->last_dir_rx = true;
    return ISO7816_OK;
}