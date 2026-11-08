/*
 * @file pc_card.h
 * @brief PC port for tests: mock HAL (virtual I/O line, virtual time)
 * + simulated smart card (ATR, PPS, T=0, T=1) driven by the test script.
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

/* Command callback: receives the complete APDU (header + data) card-side. */
typedef void (*pc_card_cmd_fn)(pc_card_t *card, const uint8_t *apdu, size_t len);

pc_card_t *pc_card_create(void);
void       pc_card_destroy(pc_card_t *card);

/* The HAL to inject into iso7816_init(). */
const iso7816_hal_t *pc_card_hal(pc_card_t *card);

/* Attaches the library context (the mock reads current F/D/f/convention). */
void pc_card_attach(pc_card_t *card, iso7816_t *dev);

/* Simulated card configuration. */
void pc_card_set_atr(pc_card_t *card, uint8_t ts, const uint8_t *atr, size_t len);
void pc_card_set_protocol(pc_card_t *card, uint8_t protocol);      /* 0 or 1: protocol expected after the ATR */
void pc_card_set_pps_accept(pc_card_t *card, bool accept);  /* echo of the PPS request */
void pc_card_set_cmd_handler(pc_card_t *card, uint8_t ins, bool incoming, pc_card_cmd_fn fn);
void pc_card_set_silent(pc_card_t *card, bool silent);      /* silent card (no ATR) */
void pc_card_corrupt_next_tx(pc_card_t *card);              /* next transmitted frame: wrong parity */

/* Card transmit queue (TPDU bytes the card will send). */
void pc_card_queue_tx(pc_card_t *card, const uint8_t *data, size_t len);
void pc_card_queue_sw(pc_card_t *card, uint16_t sw);

/* Response-building helpers. */
void pc_card_t0_queue_ack_all(pc_card_t *card, uint8_t ins);            /* ACK = INS */
void pc_card_t0_queue_data_sw(pc_card_t *card, const uint8_t *data, size_t len, uint16_t sw);
void pc_card_t1_queue_response(pc_card_t *card, const uint8_t *data, size_t len, uint16_t sw);

/* Inspection for assertions. */
size_t pc_card_rxlog(const pc_card_t *card, uint8_t *out, size_t cap); /* bytes received by the card */
uint32_t pc_card_ifd_err_signals(const pc_card_t *card);                /* error signals sent by the IFD */
uint32_t pc_card_now_us(const pc_card_t *card);

#ifdef __cplusplus
}
#endif
