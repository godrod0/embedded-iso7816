/**
 * @file iso7816_t0.h
 * @brief T=0 protocol (cl. 10) and APDU mapping (cl. 12.2).
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

/**
 * @brief Exchange a command-response pair over T=0 (cl. 12.2).
 * @param dev       Library context (protocol must be T=0).
 * @param cmd       Command APDU.
 * @param cmd_len   Command length.
 * @param resp      Response data buffer (without SW1-SW2).
 * @param resp_cap  Response buffer capacity.
 * @param resp_len  Receives the response data length.
 * @param sw        Receives SW1-SW2.
 * @return Transport status.
 */
iso7816_status_t iso7816_t0_exchange(struct iso7816 *dev,
                                     const uint8_t *cmd, size_t cmd_len,
                                     uint8_t *resp, size_t resp_cap,
                                     size_t *resp_len, uint16_t *sw);

#ifdef __cplusplus
}
#endif
