/* Helpers internes communs aux couches. */
#pragma once

#include <stdint.h>

#include "iso7816/iso7816.h"

static inline uint32_t iso7816_now(const iso7816_t *d)
{
    return d->hal->micros(d->hal->ctx);
}

/* Attend jusqu'à l'instant absolu t (gestion du wrap uint32 via diff signée). */
static inline void iso7816_wait_until(const iso7816_t *d, uint32_t t)
{
    while ((int32_t)(iso7816_now(d) - t) < 0) {
    }
}

static inline void iso7816_delay_us(const iso7816_t *d, uint32_t us)
{
    d->hal->delay_us(d->hal->ctx, us);
}

/* Cycles d'horloge -> µs. */
static inline uint32_t iso7816_cycles_us(const iso7816_t *d, uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * 1000000ULL) / d->f_hz);
}