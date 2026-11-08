/* Helpers internes communs aux couches. */
#pragma once

#include <stdint.h>

#include "iso7816/iso7816.h"

static inline uint32_t iso7816_now(const iso7816_t *dev)
{
    return dev->hal->micros(dev->hal->ctx);
}

/* Attend jusqu'à l'instant absolu deadline_us (gestion du wrap uint32 via diff signée). */
static inline void iso7816_wait_until(const iso7816_t *dev, uint32_t deadline_us)
{
    while ((int32_t)(iso7816_now(dev) - deadline_us) < 0) {
    }
}

static inline void iso7816_delay_us(const iso7816_t *dev, uint32_t duration_us)
{
    dev->hal->delay_us(dev->hal->ctx, duration_us);
}

/* Cycles d'horloge -> µs. */
static inline uint32_t iso7816_cycles_us(const iso7816_t *dev, uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * ISO7816_US_PER_SECOND) / dev->f_hz);
}
