/*
 * iso7816_t1.h — protocole T=1 (cl. 11) et mapping APDU (cl. 12.3).
 */
#pragma once

#include "iso7816_const.h"

#include <stddef.h>
#include <stdint.h>

#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iso7816;

typedef struct {
    uint8_t  nad;
    uint8_t  pcb;                   /* type de bloc + N(S)/N(R)/fonction S */
    uint8_t  len;                   /* LEN */
    uint8_t  inf[ISO7816_T1_INF_MAX];
    uint8_t  inf_len;
} iso7816_t1_block_t;

/* Échange d'une paire commande-réponse par T=1 (cl. 12.3). */
iso7816_status_t iso7816_t1_exchange(struct iso7816 *dev,
                                     const uint8_t *cmd, size_t cmd_len,
                                     uint8_t *resp, size_t resp_cap,
                                     size_t *resp_len, uint16_t *sw);

#ifdef __cplusplus
}
#endif