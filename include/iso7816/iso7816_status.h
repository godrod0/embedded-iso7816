/*
 * iso7816_status.h — codes de retour de la bibliothèque.
 *
 * Convention de nommage : ISO7816_<MODULE>_<CODE>
 *   lib    = ISO7816
 *   module = GEN, DEV, CHAR, ATR, PPS, T0, T1, APDU
 *   code   = cause
 *
 * Les SW1-SW2 de la carte ne sont PAS des codes de retour de la
 * bibliothèque : ils sont rendus séparément par iso7816_exchange().
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ISO7816_OK = 0,                 /* succès (pas une erreur) */

    /* module GEN — générique */
    ISO7816_GEN_HAL,                /* statut remonté par le port matériel (iso7816_hal_t) */
    ISO7816_GEN_PARAM,              /* argument invalide (NULL, taille, classe...) */
    ISO7816_GEN_STATE,              /* appel hors séquence (ex. exchange avant activate) */
    ISO7816_GEN_SIZE,               /* buffer de réception trop petit */

    /* module DEV — procédure carte, cl. 5-6 */
    ISO7816_DEV_NO_ANSWER,          /* pas de réponse au reset dans les 40 000 cycles */
    ISO7816_DEV_CLASS,              /* classe A/B/C non supportée par la carte */
    ISO7816_DEV_IO,                 /* conflit de pilotage sur I/O */

    /* module CHAR — trame caractère, cl. 7 */
    ISO7816_CHAR_PARITY,            /* parité incorrecte après les répétitions autorisées */
    ISO7816_CHAR_WT,                /* waiting time dépassé (émetteur muet) */

    /* module ATR — answer to reset, cl. 8 */
    ISO7816_ATR_TS,                 /* caractère initial TS invalide */
    ISO7816_ATR_MALFORMED,          /* ATR malformé (bitmap, >32 octets, TD non croissant...) */
    ISO7816_ATR_TCK,                /* TCK invalide (XOR != 0) */

    /* module PPS — cl. 9 */
    ISO7816_PPS_FAILED,             /* échange PPS non concluant / réponse invalide */
    ISO7816_PPS_PROTOCOL,           /* protocole T non offert par la carte (TD) */

    /* module T0 — cl. 10 */
    ISO7816_T0_PROC,                /* octet de procédure invalide reçu de la carte */

    /* module T1 — cl. 11 */
    ISO7816_T1_EDC,                 /* erreur LRC/CRC persistante après retransmissions */
    ISO7816_T1_SEQ,                 /* erreur de séquence N(S)/N(R) */
    ISO7816_T1_BWT,                 /* BWT dépassé : la carte ne répond plus */
    ISO7816_T1_RESYNC,              /* resynchronisation échouée (3 niveaux) */

    /* module APDU — cl. 12.1 */
    ISO7816_APDU_INVALID,           /* command APDU hors des 7 cas valides (tableau 13) */
} iso7816_status_t;

const char *iso7816_strstatus(iso7816_status_t s);

#ifdef __cplusplus
}
#endif