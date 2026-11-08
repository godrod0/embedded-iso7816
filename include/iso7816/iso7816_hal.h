/*
 * iso7816_hal.h — unique API d'accès au matériel (Hardware Abstraction Layer).
 *
 * Toute la bibliothèque passe exclusivement par cette structure de
 * pointeurs de fonctions, injectée dans iso7816_init(). Deux ports :
 *   - port/stm32u5/  : cible réelle (GPIO + TIM + commande d'alim)
 *   - port/pc/       : mock pour tests unitaires/fonctionnels sur PC
 *
 * Sémantique des broches (ISO/IEC 7816-3, cl. 5.2) :
 *   C1 VCC : alimentation (classe A=5V, B=3V, C=1,8V)
 *   C2 RST : reset (état H/L)
 *   C3 CLK : horloge
 *   C5 GND : masse (référence, gérée par le port)
 *   C6 SPU : usage standard/propriétaire (non piloté ici)
 *   C7 I/O : ligne série half-duplex
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

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

    int      (*init)(void *ctx);                        /* init périphériques (GPIO, timers, alim) */
    void     (*deinit)(void *ctx);

    void     (*vcc_on)(void *ctx, iso7816_class_t cls); /* alimente C1 selon la classe */
    void     (*vcc_off)(void *ctx);                     /* coupe l'alimentation de la carte */

    void     (*rst_drive)(void *ctx, bool high);        /* C2 : état H/L */

    uint32_t (*clk_start)(void *ctx, uint32_t hz);      /* démarre C3 ; renvoie la fréquence réelle */
    void     (*clk_stop)(void *ctx, bool keep_high);    /* arrêt d'horloge (indicateur X, cl. 6.3.2) */

    void     (*io_tx)(void *ctx, bool high);            /* C7 en sortie, impose H/L */
    void     (*io_rx)(void *ctx);                       /* C7 en entrée (haute impédance + pull-up vers VCC) */
    bool     (*io_read)(void *ctx);                     /* lit l'état de C7 */

    uint32_t (*micros)(void *ctx);                      /* base de temps en µs (pour l'échantillonnage à (n±0,2) etu) */
    void     (*delay_us)(void *ctx, uint32_t us);       /* sommeil bloquant */
} iso7816_hal_t;

#ifdef __cplusplus
}
#endif