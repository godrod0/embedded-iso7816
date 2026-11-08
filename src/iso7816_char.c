#include "iso7816/iso7816_char.h"

#include "iso7816_internal.h"

uint32_t iso7816_etu_us(const iso7816_t *dev)
{
    return (uint32_t)(((uint64_t)dev->F * ISO7816_US_PER_SECOND) /
                      ((uint64_t)dev->D * dev->f_hz));
}

uint32_t iso7816_gt_us(const iso7816_t *dev)
{
    uint32_t etu = iso7816_etu_us(dev);
    uint32_t guard_us = ISO7816_CHAR_GT_ETU * etu;
    if (dev->guard_n == ISO7816_GUARD_N_NONE || dev->guard_n == ISO7816_GUARD_N_SPECIAL) {
        /* N=255 : GT = 12 etu en PPS et T=0 (cl. 8.3) */
        return guard_us;
    }
    /* GT = 12 etu + R*N/f ; R = F/D, ou Fi/Di si T=15 présent (cl. 8.3) */
    uint64_t extra;
    if (dev->has_t15) {
        extra = ((uint64_t)dev->atr.fi * dev->guard_n * ISO7816_US_PER_SECOND) /
                ((uint64_t)dev->atr.di * dev->f_hz);
    } else {
        extra = (uint64_t)dev->guard_n * etu;
    }
    return guard_us + (uint32_t)extra;
}

static bool bit_level(const iso7816_t *dev, bool value)
{
    /* direct ('3B') : H encode 1 ; inverse ('3F') : L encode 1 */
    return dev->inverse ? !value : value;
}

iso7816_status_t iso7816_char_tx(iso7816_t *dev, uint8_t byte)
{
    const uint32_t etu = iso7816_etu_us(dev);
    const uint32_t guard_us = iso7816_gt_us(dev);

    for (uint8_t attempt = 0; attempt <= dev->max_retries; attempt++) {
        uint32_t t_start = iso7816_now(dev);
        dev->last_tx_lead_us = t_start;

        dev->hal->io_tx(dev->hal->ctx, false);          /* moment 1 : start L */

        for (int moment = (int)ISO7816_CHAR_MOMENT_FIRST_DATA;
             moment <= (int)ISO7816_CHAR_MOMENT_LAST_DATA; moment++) {
            /* inverse : msb d'abord ; direct : lsb d'abord */
            int bit_pos = dev->inverse ? ((int)ISO7816_CHAR_MOMENT_LAST_DATA - moment)
                                       : (moment - (int)ISO7816_CHAR_MOMENT_FIRST_DATA);
            bool value = ((byte >> bit_pos) & 1U) != 0;
            iso7816_wait_until(dev, t_start + (uint32_t)(moment - (int)ISO7816_CHAR_MOMENT_START) * etu);
            dev->hal->io_tx(dev->hal->ctx, bit_level(dev, value));
        }

        /* moment 10 : parité paire sur les 9 moments 2..10 (cl. 8.1) */
        bool parity = (((unsigned)__builtin_popcount(byte) & 1U) != 0U);
        iso7816_wait_until(dev, t_start + (uint32_t)ISO7816_CHAR_MOMENT_LAST_DATA * etu);
        dev->hal->io_tx(dev->hal->ctx, bit_level(dev, parity));

        iso7816_wait_until(dev, t_start + ISO7816_CHAR_MOMENT_TOTAL * etu);
        dev->hal->io_rx(dev->hal->ctx);                 /* libération de la ligne */

        /* détection du signal d'erreur à (11 ± 0,2) etu (cl. 7.3) */
        iso7816_wait_until(dev, t_start + ISO7816_CHAR_ERR_DETECT_ETU * etu);
        if (!dev->hal->io_read(dev->hal->ctx)) {
            /* erreur : attendre >= 2 etu après détection, puis répéter */
            iso7816_wait_until(dev, t_start + ISO7816_CHAR_ERR_RETRY_ETU * etu);
            continue;
        }

        /* guard time entre leading edges de caractères consécutifs (cl. 7.2) */
        iso7816_wait_until(dev, t_start + guard_us);
        dev->last_dir_rx = false;
        return ISO7816_OK;
    }
    return ISO7816_CHAR_PARITY;
}

iso7816_status_t iso7816_char_rx(iso7816_t *dev, uint8_t *byte, uint32_t timeout_us)
{
    const uint32_t etu = iso7816_etu_us(dev);
    const uint32_t start_time_us = iso7816_now(dev);

    /* attente du front start (passage à L), confirmé avant 0,5 etu (cl. 7.2) */
    uint32_t t_start = 0;
    for (;;) {
        if (!dev->hal->io_read(dev->hal->ctx)) {
            t_start = iso7816_now(dev);
            iso7816_wait_until(dev, t_start + etu / ISO7816_CHAR_HALF_ETU_DIV);
            if (!dev->hal->io_read(dev->hal->ctx)) {
                break;   /* start confirmé : toujours L à 0,5 etu */
            }
        }
        if ((int32_t)(iso7816_now(dev) - start_time_us) >= (int32_t)timeout_us) {
            return ISO7816_CHAR_WT;
        }
    }

    uint8_t value = 0;
    unsigned ones_count = 0;
    for (int moment = (int)ISO7816_CHAR_MOMENT_FIRST_DATA;
         moment <= (int)ISO7816_CHAR_MOMENT_TOTAL; moment++) {
        /* échantillon à (m - 0,5) ± 0,2 etu */
        iso7816_wait_until(dev, t_start +
                           (uint32_t)((uint32_t)(2 * moment - 1) * etu / ISO7816_CHAR_HALF_ETU_DIV));
        bool level = dev->hal->io_read(dev->hal->ctx);
        bool bit = dev->inverse ? !level : level;
        ones_count += bit ? 1U : 0U;
        if (moment <= (int)ISO7816_CHAR_MOMENT_LAST_DATA) {
            int bit_index = dev->inverse ? ((int)ISO7816_CHAR_MOMENT_LAST_DATA - moment)
                                         : (moment - (int)ISO7816_CHAR_MOMENT_FIRST_DATA);
            if (bit) {
                value |= (uint8_t)(1U << bit_index);
            }
        }
    }

    if ((ones_count & 1U) != 0U) {
        /* parité incorrecte : signal d'erreur à (10,5±0,2) etu, 1..2 etu (cl. 7.3) */
        if (dev->use_error_signal) {
            iso7816_wait_until(dev, t_start +
                               (uint32_t)(ISO7816_CHAR_ERR_SIGNAL_HALF_ETU * etu /
                                          ISO7816_CHAR_HALF_ETU_DIV)); /* 10,5 etu */
            dev->hal->io_tx(dev->hal->ctx, false);
            iso7816_wait_until(dev, t_start + ISO7816_CHAR_ERR_PULSE_END_ETU * etu); /* ~1,5 etu */
            dev->hal->io_rx(dev->hal->ctx);
        }
        return ISO7816_CHAR_PARITY;
    }

    *byte = value;
    dev->last_rx_lead_us = t_start;
    dev->last_dir_rx = true;
    return ISO7816_OK;
}
