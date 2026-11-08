/*
 * pc_card.h — port PC pour tests : mock HAL (ligne I/O virtuelle, temps virtuel)
 * + carte à puce simulée (ATR, PPS, T=0, T=1) pilotée par script de test.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iso7816/iso7816.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pc_card pc_card_t;

/* Callback de commande : reçoit l'APDU complète (header + data) côté carte. */
typedef void (*pc_card_cmd_fn)(pc_card_t *card, const uint8_t *apdu, size_t len);

pc_card_t *pc_card_create(void);
void       pc_card_destroy(pc_card_t *c);

/* La HAL à injecter dans iso7816_init(). */
const iso7816_hal_t *pc_card_hal(pc_card_t *c);

/* Attache le contexte bibliothèque (le mock lit F/D/f/convention courants). */
void pc_card_attach(pc_card_t *c, iso7816_t *dev);

/* Configuration de la carte simulée. */
void pc_card_set_atr(pc_card_t *c, uint8_t ts, const uint8_t *atr, size_t len);
void pc_card_set_protocol(pc_card_t *c, uint8_t t);      /* 0 ou 1 : protocole attendu après l'ATR */
void pc_card_set_pps_accept(pc_card_t *c, bool accept);  /* écho du PPS request */
void pc_card_set_cmd_handler(pc_card_t *c, uint8_t ins, bool incoming, pc_card_cmd_fn fn);
void pc_card_set_silent(pc_card_t *c, bool silent);      /* carte muette (pas d'ATR) */
void pc_card_corrupt_next_tx(pc_card_t *c);              /* prochaine trame émise : parité faux */

/* File d'émission carte (octets TPDU que la carte va envoyer). */
void pc_card_queue_tx(pc_card_t *c, const uint8_t *b, size_t n);
void pc_card_queue_sw(pc_card_t *c, uint16_t sw);

/* Helpers de construction de réponses. */
void pc_card_t0_queue_ack_all(pc_card_t *c, uint8_t ins);            /* ACK = INS */
void pc_card_t0_queue_data_sw(pc_card_t *c, const uint8_t *data, size_t n, uint16_t sw);
void pc_card_t1_queue_response(pc_card_t *c, const uint8_t *data, size_t n, uint16_t sw);

/* Inspection pour les assertions. */
size_t pc_card_rxlog(const pc_card_t *c, uint8_t *out, size_t cap); /* octets reçus par la carte */
uint32_t pc_card_ifd_err_signals(const pc_card_t *c);                /* signaux d'erreur émis par l'IFD */
uint32_t pc_card_now_us(const pc_card_t *c);

#ifdef __cplusplus
}
#endif/*
 * pc_card.h — port PC pour tests : mock HAL (ligne I/O virtuelle, temps virtuel)
 * + carte à puce simulée (ATR, PPS, T=0, T=1) pilotée par script de test.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iso7816/iso7816.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pc_card pc_card_t;

/* Callback de commande : reçoit l'APDU complète (header + data) côté carte. */
typedef void (*pc_card_cmd_fn)(pc_card_t *card, const uint8_t *apdu, size_t len);

pc_card_t *pc_card_create(void);
void       pc_card_destroy(pc_card_t *c);

/* La HAL à injecter dans iso7816_init(). */
const iso7816_hal_t *pc_card_hal(pc_card_t *c);

/* Attache le contexte bibliothèque (le mock lit F/D/f/convention courants). */
void pc_card_attach(pc_card_t *c, iso7816_t *dev);

/* Configuration de la carte simulée. */
void pc_card_set_atr(pc_card_t *c, uint8_t ts, const uint8_t *atr, size_t len);
void pc_card_set_protocol(pc_card_t *c, uint8_t t);      /* 0 ou 1 : protocole attendu après l'ATR */
void pc_card_set_pps_accept(pc_card_t *c, bool accept);  /* écho du PPS request */
void pc_card_set_cmd_handler(pc_card_t *c, uint8_t ins, bool incoming, pc_card_cmd_fn fn);
void pc_card_set_silent(pc_card_t *c, bool silent);      /* carte muette (pas d'ATR) */
void pc_card_corrupt_next_tx(pc_card_t *c);              /* prochaine trame émise : parité faux */

/* File d'émission carte (octets TPDU que la carte va envoyer). */
void pc_card_queue_tx(pc_card_t *c, const uint8_t *b, size_t n);
void pc_card_queue_sw(pc_card_t *c, uint16_t sw);

/* Helpers de construction de réponses. */
void pc_card_t0_queue_ack_all(pc_card_t *c, uint8_t ins);            /* ACK = INS */
void pc_card_t0_queue_data_sw(pc_card_t *c, const uint8_t *data, size_t n, uint16_t sw);
void pc_card_t1_queue_response(pc_card_t *c, const uint8_t *data, size_t n, uint16_t sw);

/* Inspection pour les assertions. */
size_t pc_card_rxlog(const pc_card_t *c, uint8_t *out, size_t cap); /* octets reçus par la carte */
uint32_t pc_card_ifd_err_signals(const pc_card_t *c);                /* signaux d'erreur émis par l'IFD */
uint32_t pc_card_now_us(const pc_card_t *c);

#ifdef __cplusplus
}
#endif