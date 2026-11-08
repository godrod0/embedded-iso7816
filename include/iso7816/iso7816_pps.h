/**
 * @file iso7816_pps.h
 * @brief Protocol and Parameters Selection (cl. 9).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "iso7816_const.h"
#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iso7816;

/**
 * @brief PPS exchange (cl. 9): propose protocol T and F/D parameters.
 * On success, updates dev->F, dev->D and dev->protocol.
 * F/D must be within [Fd..Fi] x [Dd..Di] (cl. 9.2).
 * @param dev      Library context (must be active).
 * @param protocol Requested protocol T.
 * @param F        Requested clock-rate integer.
 * @param D        Requested bit-rate integer.
 * @return ISO7816_OK or ISO7816_PPS_* / GEN status.
 */
iso7816_status_t iso7816_pps_exchange(struct iso7816 *dev, uint8_t protocol,
                                      uint16_t F, uint8_t D);

#ifdef __cplusplus
}
#endif
