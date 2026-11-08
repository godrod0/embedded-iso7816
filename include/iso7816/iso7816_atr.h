/*
 * iso7816_atr.h — Answer-to-Reset (cl. 8).
 */
#pragma once

#include "iso7816_const.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* octet brut TS ('3B' direct / '3F' inverse) puis T0..TCK */
    uint8_t  raw[1 + ISO7816_ATR_MAX_BYTES];   /* TS en [0], puis T0..TCK */
    size_t   raw_len;               /* nombre d'octets reçus (TS inclus) */

    bool     inverse;               /* convention de codage déduite de TS */

    uint8_t  t0;
    uint8_t  k;                     /* nombre d'octets historiques (T0 bits 4..1) */

    uint8_t  protocols[ISO7816_ATR_PROTO_MAX];
    uint8_t  n_protocols;           /* types T offerts, ordre croissant (cl. 8.2.3) */

    /* octets globaux */
    bool     has_ta1;
    uint16_t fi;                    /* valeur indiquée de F (défaut 372) */
    uint8_t  di;                    /* valeur indiquée de D (défaut 1) */
    uint8_t  fmax_mhz;              /* fréquence max supportée par la carte */
    uint8_t  guard_n;               /* TC1 : entier de garde supplémentaire N (défaut 0) */
    bool     has_ta2;
    uint8_t  ta2;                   /* mode spécifique (cl. 8.3) */

    /* T=15 */
    bool     has_t15;
    uint8_t  x_clock_stop;          /* 0=non supporté, 1=L, 2=H, 3=sans préférence (tableau 9) */
    uint8_t  y_class;               /* masque de classes : bit1=A, bit2=B, bit3=C (tableau 10) */
    bool     spu_used;
    bool     spu_standard;

    /* spécifiques T=0 */
    uint8_t  wi;                    /* TC2 : waiting time integer (défaut 10) */

    /* spécifiques T=1 (cl. 11.4) */
    bool     has_t1;
    uint8_t  ifsc;                  /* défaut 32 */
    uint8_t  cwi;                   /* défaut 13 */
    uint8_t  bwi;                   /* défaut 4 */
    bool     use_crc;               /* 1er TC pour T=1, bit 1 */

    uint8_t  historical[ISO7816_ATR_HIST_MAX];
    uint8_t  hist_len;

    bool     tck_present;
    bool     tck_ok;                /* XOR de T0..TCK == 0 */
} iso7816_atr_t;

/*
 * Parse un ATR reçu. buf contient les octets à partir de T0 (TS passé
 * séparément via ts). raw_len = nombre d'octets de buf (T0..TCK).
 */
iso7816_status_t iso7816_atr_parse(iso7816_atr_t *atr, uint8_t ts,
                                    const uint8_t *buf, size_t len);

/* Indique si la séquence reçue (T0..courant) est complète (TCK inclus si requis). */
bool iso7816_atr_complete(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif