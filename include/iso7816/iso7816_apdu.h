/**
 * @file iso7816_apdu.h
 * @brief APDU (cl. 12.1): decoding of the 7 cases (table 13) and encoding.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "iso7816_const.h"
#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief APDU case (table 13, cl. 12.1.3). */
typedef enum {
    ISO7816_APDU_CASE_1 = 1,    /**< No data, no response data */
    ISO7816_APDU_CASE_2S,       /**< Short: no data, Le response */
    ISO7816_APDU_CASE_2E,       /**< Extended: no data, Le response */
    ISO7816_APDU_CASE_3S,       /**< Short: Lc data, no response data */
    ISO7816_APDU_CASE_3E,       /**< Extended: Lc data, no response data */
    ISO7816_APDU_CASE_4S,       /**< Short: Lc data + Le response */
    ISO7816_APDU_CASE_4E,       /**< Extended: Lc data + Le response */
} iso7816_apdu_case_t;

/** @brief Decoded command APDU. */
typedef struct {
    uint8_t  cla, ins, p1, p2;
    iso7816_apdu_case_t apdu_case;
    uint32_t nc;            /**< Bytes in the command data field */
    const uint8_t *data;    /**< Command data field (NULL if nc=0) */
    uint32_t ne;            /**< Max expected in response (0 = no response data expected) */
} iso7816_apdu_cmd_t;

/**
 * @brief Decode a command APDU (cl. 12.1.3, table 13).
 * @param buf Command buffer.
 * @param len Length in bytes.
 * @param out Receives the decoded APDU.
 * @return ISO7816_OK or ISO7816_APDU_INVALID.
 */
iso7816_status_t iso7816_apdu_decode(const uint8_t *buf, size_t len,
                                     iso7816_apdu_cmd_t *out);

/**
 * @brief Encode a command APDU.
 * @param cmd Decoded APDU to encode.
 * @param out Output buffer.
 * @param cap Output buffer capacity.
 * @return Number of bytes written, or 0 if cap is too small.
 */
size_t iso7816_apdu_encode(const iso7816_apdu_cmd_t *cmd, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
