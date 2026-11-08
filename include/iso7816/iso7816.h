/*
 * iso7816.h — API principale de la bibliothèque (côté dispositif d'interface / IFD).
 *
 * Séquence type (cl. 6.1) :
 *   iso7816_init() -> iso7816_activate() -> [iso7816_pps()] -> iso7816_exchange()* -> iso7816_deactivate()
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iso7816_atr.h"
#include "iso7816_hal.h"
#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const iso7816_hal_t *hal;   /* API matérielle (obligatoire) */
    uint32_t clk_hz;            /* horloge pendant activation/reset/ATR : 1..5 MHz (défaut 1 MHz) */
    uint32_t timeout_ms;        /* timeout global de secours (défaut 1000 ms) */
    uint8_t  max_retries;       /* répétitions de caractère max (cl. 7.3, défaut 3) */
} iso7816_config_t;

typedef struct iso7816 {
    const iso7816_hal_t *hal;

    iso7816_class_t cls;        /* classe appliquée */
    uint32_t f_hz;              /* fréquence CLK courante */
    uint16_t F;                 /* entier de conversion d'horloge courant */
    uint8_t  D;                 /* entier d'ajustement de débit courant */
    bool     inverse;           /* convention de codage (TS) */
    uint8_t  guard_n;           /* TC1 : N */
    bool     has_t15;
    uint32_t wt_us;             /* waiting time courant en µs */
    uint32_t timeout_ms;
    uint8_t  max_retries;
    bool     active;            /* carte activée (ATR reçu) */
    uint8_t  protocol;          /* protocole retenu (T=0 ou T=1) */
    bool     use_error_signal;  /* signal d'erreur/répétition (cl. 7.3) : oui sauf T=1 */

    iso7816_atr_t atr;

    /* T=0 (cl. 10.2) */
    uint8_t  wi;

    /* T=1 (cl. 11.4) */
    uint8_t  ifsc, ifsd;
    uint8_t  cwi, bwi;
    bool     use_crc;
    uint8_t  ns_ifd;            /* prochain N(S) des I-blocs de l'IFD */
    uint8_t  ns_card;           /* dernier N(S) reçu de la carte */

    uint32_t last_tx_lead_us;   /* leading edge du dernier caractère émis (GT) */
    uint32_t last_rx_lead_us;   /* leading edge du dernier caractère reçu (BGT, T=1) */
    bool     last_dir_rx;       /* dernière direction était réception (BGT) */
    bool     clock_stopped;     /* horloge arrêtée (cl. 6.3.2) */
    uint32_t clk_reset_hz;      /* horloge appliquée pendant activation/reset/ATR */
    uint32_t atr_end_us;        /* fin de réception du dernier ATR */
} iso7816_t;

iso7816_status_t iso7816_init(iso7816_t *d, const iso7816_hal_t *hal,
                              const iso7816_config_t *cfg);

/* Activation + cold reset + ATR (cl. 6.2.1, 6.2.2, 8). Sélectionne la classe. */
iso7816_status_t iso7816_activate(iso7816_t *d, iso7816_class_t cls);

/* Warm reset + nouvel ATR (cl. 6.2.3). */
iso7816_status_t iso7816_warm_reset(iso7816_t *d);

/* PPS optionnel (cl. 9) — voir iso7816_pps_exchange(). */
iso7816_status_t iso7816_pps(iso7816_t *d, uint8_t protocol, uint16_t F, uint8_t D);

/*
 * Échange d'une paire commande-réponse (cl. 12) via le protocole retenu :
 * command APDU -> response APDU (data + SW1-SW2).
 * resp_len reçoit la taille des données de réponse (hors SW1-SW2) ;
 * sw reçoit SW1-SW2 (ex. 0x9000). Un SW d'erreur n'est pas un code de
 * retour de transport : la fonction renvoie ISO7816_OK avec *sw != 0x9000.
 */
iso7816_status_t iso7816_exchange(iso7816_t *d,
                                  const uint8_t *cmd, size_t cmd_len,
                                  uint8_t *resp, size_t resp_cap,
                                  size_t *resp_len, uint16_t *sw);

/* Arrêt / reprise d'horloge si supporté (cl. 6.3.2, indicateur X). */
iso7816_status_t iso7816_clock_stop(iso7816_t *d);
iso7816_status_t iso7816_clock_restart(iso7816_t *d);

/* Désactivation (cl. 6.4) : RST=L, CLK=L, I/O=L, VCC off. */
iso7816_status_t iso7816_deactivate(iso7816_t *d);

const iso7816_atr_t *iso7816_get_atr(const iso7816_t *d);
uint8_t iso7816_get_protocol(const iso7816_t *d);

#ifdef __cplusplus
}
#endif