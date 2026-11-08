/*
 * iso7816_apdu.h — APDU (cl. 12.1) : décodage des 7 cas (tableau 13) et encodage.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ISO7816_APDU_CASE_1 = 1,
    ISO7816_APDU_CASE_2S,
    ISO7816_APDU_CASE_2E,
    ISO7816_APDU_CASE_3S,
    ISO7816_APDU_CASE_3E,
    ISO7816_APDU_CASE_4S,
    ISO7816_APDU_CASE_4E,
} iso7816_apdu_case_t;

typedef struct {
    uint8_t  cla, ins, p1, p2;
    iso7816_apdu_case_t apdu_case;
    uint32_t nc;            /* octets dans le champ data de commande */
    const uint8_t *data;    /* champ data de commande (NULL si nc=0) */
    uint32_t ne;            /* max attendu en réponse (0 = aucune donnée attendue) */
} iso7816_apdu_cmd_t;

/* Décode un command APDU (cl. 12.1.3, tableau 13). */
iso7816_status_t iso7816_apdu_decode(const uint8_t *buf, size_t len,
                                     iso7816_apdu_cmd_t *out);

/* Encode un command APDU. Renvoie la taille écrite, ou 0 si cap trop petit. */
size_t iso7816_apdu_encode(const iso7816_apdu_cmd_t *c, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif