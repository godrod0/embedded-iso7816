/*
 * iso7816_char.h — couche caractère asynchrone (cl. 7).
 *
 * Trame de 10 moments : start (L) + 8 bits + parité (paire).
 * etu = F/D cycles d'horloge. Signal d'erreur à (10,5±0,2) etu (cl. 7.3).
 */
#pragma once

#include <stdint.h>

#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iso7816;

/* Durée d'un etu en µs pour les paramètres courants F/D/f. */
uint32_t iso7816_etu_us(const struct iso7816 *d);

/* Guard time en µs (cl. 8.3, TC1) : 12 etu + R*N/f. */
uint32_t iso7816_gt_us(const struct iso7816 *d);

/* Émet un caractère ; gère le signal d'erreur et la répétition (cl. 7.3). */
iso7816_status_t iso7816_char_tx(struct iso7816 *d, uint8_t b);

/*
 * Reçoit un caractère. timeout_us : waiting time (WT) en µs.
 * En cas de parité incorrecte : si d->use_error_signal, émet le signal
 * d'erreur (cl. 7.3) ; renvoie ISO7816_CHAR_PARITY (l'appelant retente).
 */
iso7816_status_t iso7816_char_rx(struct iso7816 *d, uint8_t *b, uint32_t timeout_us);

#ifdef __cplusplus
}
#endif