/**
 * @file iso7816_char.h
 * @brief Asynchronous character layer (cl. 7).
 *
 * 10-moment frame: start (L) + 8 bits + even parity. etu = F/D clock cycles.
 * The full frame, parity check, error signal (NACK) and guard time are
 * handled by the port (smartcard peripheral); this layer keeps the
 * retransmission policy and waiting times.
 */
#pragma once

#include <stdint.h>

#include "iso7816_const.h"
#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct iso7816;

/** @brief Duration of one etu in µs for the current F/D/f parameters. */
uint32_t iso7816_etu_us(const struct iso7816 *dev);

/** @brief Guard time in µs (cl. 8.3, TC1): 12 etu + R*N/f. */
uint32_t iso7816_gt_us(const struct iso7816 *dev);

/**
 * @brief Transmit one character; manages the error signal and retransmission (cl. 7.3).
 * @param dev  Library context.
 * @param byte Byte to send.
 * @return ISO7816_OK or ISO7816_CHAR_PARITY after max_retries.
 */
iso7816_status_t iso7816_char_tx(struct iso7816 *dev, uint8_t byte);

/**
 * @brief Receive one character.
 * @param dev        Library context.
 * @param byte       Receives the byte.
 * @param timeout_us Waiting time (WT) in µs.
 * @return ISO7816_OK, ISO7816_CHAR_WT (timeout) or ISO7816_CHAR_PARITY
 *         (wrong parity; the port sent the error signal, cl. 7.3 — caller retries).
 */
iso7816_status_t iso7816_char_rx(struct iso7816 *dev, uint8_t *byte, uint32_t timeout_us);

#ifdef __cplusplus
}
#endif
