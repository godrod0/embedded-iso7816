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

/*
 * Émet un caractère. La trame complète (start + 8 bits + parité, cl. 7.2)
 * et le guard time sont gérés par le port (périphérique smartcard).
 * En cas de signal d'erreur reçu (NACK, cl. 7.3) le port renvoie
 * ISO7816_CHAR_PARITY : on répète jusqu'à max_retries.
 */
iso7816_status_t iso7816_char_tx(iso7816_t *dev, uint8_t byte)
{
    for (uint8_t attempt = 0; attempt <= dev->max_retries; attempt++) {
        dev->last_tx_lead_us = iso7816_now(dev);
        iso7816_status_t stat = dev->hal->tx_byte(dev->hal->ctx, byte);
        dev->last_dir_rx = false;
        if (stat == ISO7816_OK) return ISO7816_OK;
        if (stat != ISO7816_CHAR_PARITY) return stat;
        /* PARITY : signal d'erreur reçu, on répète le caractère (cl. 7.3) */
    }
    return ISO7816_CHAR_PARITY;
}

/*
 * Reçoit un caractère. timeout_us = waiting time (WT).
 * Le port gère la trame et émet le signal d'erreur/NACK si la parité est
 * fausse (cl. 7.3) ; il renvoie ISO7816_CHAR_PARITY dans ce cas.
 */
iso7816_status_t iso7816_char_rx(iso7816_t *dev, uint8_t *byte, uint32_t timeout_us)
{
    iso7816_status_t stat = dev->hal->rx_byte(dev->hal->ctx, byte, timeout_us);
    if (stat == ISO7816_OK) {
        dev->last_rx_lead_us = iso7816_now(dev);
        dev->last_dir_rx = true;
    }
    return stat;
}
