/*
 * iso7816_t0.h — protocole T=0 (cl. 10) et mapping APDU (cl. 12.2).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iso7816;

/* Échange d'une paire commande-réponse par T=0 (cl. 12.2). */
iso7816_status_t iso7816_t0_exchange(struct iso7816 *d,
                                     const uint8_t *cmd, size_t cmd_len,
                                     uint8_t *resp, size_t resp_cap,
                                     size_t *resp_len, uint16_t *sw);

#ifdef __cplusplus
}
#endif