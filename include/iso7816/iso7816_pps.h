/*
 * iso7816_pps.h — Protocol and Parameters Selection (cl. 9).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iso7816;

/*
 * Échange PPS (cl. 9) : propose le protocole T et les paramètres F/D.
 * En cas de succès, met à jour d->F, d->D et d->protocol.
 * F/D doivent être dans [Fd..Fi] x [Dd..Di] (cl. 9.2).
 */
iso7816_status_t iso7816_pps_exchange(struct iso7816 *d, uint8_t protocol,
                                      uint16_t F, uint8_t D);

#ifdef __cplusplus
}
#endif