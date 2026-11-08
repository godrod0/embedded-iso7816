/**
 * @file iso7816_t1.h
 * @brief T=1 protocol (cl. 11) and APDU mapping (cl. 12.3).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "iso7816_const.h"
#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iso7816;

/** @brief One T=1 block: NAD, PCB, LEN, INF, EDC. */
typedef struct {
    uint8_t  nad;
    uint8_t  pcb;                   /**< Block type + N(S)/N(R)/S-function */
    uint8_t  len;                   /**< LEN */
    uint8_t  inf[ISO7816_T1_INF_MAX];
    uint8_t  inf_len;
} iso7816_t1_block_t;

/**
 * @brief Exchange a command-response pair over T=1 (cl. 12.3).
 * @param dev       Library context (protocol must be T=1).
 * @param cmd       Command APDU.
 * @param cmd_len   Command length.
 * @param resp      Response data buffer (without SW1-SW2).
 * @param resp_cap  Response buffer capacity.
 * @param resp_len  Receives the response data length.
 * @param sw        Receives SW1-SW2.
 * @return Transport status.
 */
iso7816_status_t iso7816_t1_exchange(struct iso7816 *dev,
                                     const uint8_t *cmd, size_t cmd_len,
                                     uint8_t *resp, size_t resp_cap,
                                     size_t *resp_len, uint16_t *sw);

#ifdef __cplusplus
}
#endif
