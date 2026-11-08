/**
 * @file iso7816_status.h
 * @brief Library return codes.
 *
 * Naming convention: ISO7816_<MODULE>_<CODE>
 *   lib    = ISO7816
 *   module = GEN, DEV, CHAR, ATR, PPS, T0, T1, APDU
 *   code   = cause
 *
 * Card SW1-SW2 are NOT library return codes: they are returned separately
 * by iso7816_exchange().
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Library status codes. */
typedef enum {
    ISO7816_OK = 0,                 /**< Success (not an error) */

    /* module GEN — generic */
    ISO7816_GEN_HAL,                /**< Status reported by the hardware port (iso7816_hal_t) */
    ISO7816_GEN_PARAM,              /**< Invalid argument (NULL, size, class...) */
    ISO7816_GEN_STATE,              /**< Call out of sequence (e.g. exchange before activate) */
    ISO7816_GEN_SIZE,               /**< Reception buffer too small */

    /* module DEV — card procedure, cl. 5-6 */
    ISO7816_DEV_NO_ANSWER,          /**< No answer to reset within 40 000 cycles */
    ISO7816_DEV_CLASS,              /**< Class A/B/C not supported by the card */
    ISO7816_DEV_IO,                 /**< I/O drive conflict */

    /* module CHAR — character frame, cl. 7 */
    ISO7816_CHAR_PARITY,            /**< Parity wrong after allowed retransmissions */
    ISO7816_CHAR_WT,                /**< Waiting time exceeded (sender silent) */

    /* module ATR — answer to reset, cl. 8 */
    ISO7816_ATR_TS,                 /**< Invalid initial character TS */
    ISO7816_ATR_MALFORMED,          /**< Malformed ATR (bitmap, >32 bytes, non-increasing TD...) */
    ISO7816_ATR_TCK,                /**< Invalid TCK (XOR != 0) */

    /* module PPS — cl. 9 */
    ISO7816_PPS_FAILED,             /**< PPS exchange inconclusive / invalid response */
    ISO7816_PPS_PROTOCOL,           /**< Protocol T not offered by the card (TD) */

    /* module T0 — cl. 10 */
    ISO7816_T0_PROC,                /**< Invalid procedure byte received from the card */

    /* module T1 — cl. 11 */
    ISO7816_T1_EDC,                 /**< Persistent LRC/CRC error after retransmissions */
    ISO7816_T1_SEQ,                 /**< N(S)/N(R) sequence error */
    ISO7816_T1_BWT,                 /**< BWT exceeded: the card no longer answers */
    ISO7816_T1_RESYNC,              /**< Resynchronization failed (3 levels) */

    /* module APDU — cl. 12.1 */
    ISO7816_APDU_INVALID,           /**< Command APDU outside the 7 valid cases (table 13) */
} iso7816_status_t;

/** @brief Human-readable string for a status code. */
const char *iso7816_strstatus(iso7816_status_t stat);

#ifdef __cplusplus
}
#endif
