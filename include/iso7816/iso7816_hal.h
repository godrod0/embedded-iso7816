/*
 * iso7816_hal.h — unique API d'accès au matériel (Hardware Abstraction Layer).
 *
 * Toute la bibliothèque passe exclusivement par cette structure de
 * pointeurs de fonctions, injectée dans iso7816_init(). Deux ports :
 *   - port/stm32u5/  : USART en mode smartcard (HAL SMARTCARD STM32U5) :
 *                      le périphérique gère la trame (start + 8 bits + parité,
 *                      NACK, guard time) et l'horloge sur CK
 *   - tests/pc_card  : mock pour tests unitaires/fonctionnels sur PC
 *
 * Sémantique des broches (ISO/IEC 7816-3, cl. 5.2) :
 *   C1 VCC : alimentation (classe A=5V, B=3V, C=1,8V) — vcc_on/vcc_off
 *   C2 RST : reset (état H/L) — rst_drive
 *   C3 CLK : horloge — clk_start/clk_stop (CK du périphérique smartcard)
 *   C5 GND : masse (référence, gérée par le port)
 *   C6 SPU : non piloté ici
 *   C7 I/O : ligne série half-duplex — tx_byte/rx_byte (TX du périphérique
 *            smartcard, open-drain) ; io_read pour scruter la ligne
 *            (détection de réponse, clock stop) ; io_drive pour forcer
 *            la ligne (désactivation, cl. 6.4)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Classes de conditions de fonctionnement (cl. 5.1.3) */
typedef enum {
    ISO7816_CLASS_A = 5,    /* 5 V   */
    ISO7816_CLASS_B = 3,    /* 3 V   */
    ISO7816_CLASS_C = 1,    /* 1,8 V */
} iso7816_class_t;

typedef struct iso7816_hal {
    void *ctx;      /* opaque, renvoyé tel quel à chaque callback */

    int      (*init)(void *ctx);                        /* init périphériques (GPIO, USART, timers, alim) */
    void     (*deinit)(void *ctx);

    void     (*vcc_on)(void *ctx, iso7816_class_t cls); /* alimente C1 selon la classe */
    void     (*vcc_off)(void *ctx);                     /* coupe l'alimentation de la carte */

    void     (*rst_drive)(void *ctx, bool high);        /* C2 : état H/L */

    uint32_t (*clk_start)(void *ctx, uint32_t hz);      /* démarre C3 ; renvoie la fréquence réelle */
    void     (*clk_stop)(void *ctx, bool keep_high);    /* arrêt d'horloge (indicateur X, cl. 6.3.2) */

    /* Niveau caractère : le port émet/reçoit la trame complète
       (10 etu : start + 8 bits + parité paire ; NACK et guard time gérés
       par le port). rx_byte : timeout_us = waiting time, renvoie
       ISO7816_CHAR_WT si l'émetteur est muet, ISO7816_CHAR_PARITY si la
       parité est fausse (le port a émis le signal d'erreur / NACK, cl. 7.3). */
    iso7816_status_t (*tx_byte)(void *ctx, uint8_t byte);
    iso7816_status_t (*rx_byte)(void *ctx, uint8_t *byte, uint32_t timeout_us);

    bool     (*io_read)(void *ctx);                     /* lit l'état de C7 */
    void     (*io_drive)(void *ctx, bool high);         /* force C7 en sortie H/L (désactivation) */

    uint32_t (*micros)(void *ctx);                      /* base de temps en µs (BGT, attentes) */
    void     (*delay_us)(void *ctx, uint32_t us);       /* sommeil bloquant */
} iso7816_hal_t;

#ifdef __cplusplus
}
#endif
