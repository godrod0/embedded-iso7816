/**
 * @file iso7816_atr.h
 * @brief Answer-to-Reset (cl. 8).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iso7816_const.h"
#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Parsed ATR. */
typedef struct {
    /* raw bytes: TS ('3B' direct / '3F' inverse) then T0..TCK */
    uint8_t  raw[1 + ISO7816_ATR_MAX_BYTES];   /**< TS at [0], then T0..TCK */
    size_t   raw_len;               /**< Number of received bytes (TS included) */

    bool     inverse;               /**< Coding convention derived from TS */

    uint8_t  t0;
    uint8_t  k;                     /**< Number of historical bytes (T0 bits 4..1) */

    uint8_t  protocols[ISO7816_ATR_PROTO_MAX];
    uint8_t  n_protocols;           /**< Offered T types, increasing order (cl. 8.2.3) */

    /* global interface bytes */
    bool     has_ta1;
    uint16_t fi;                    /**< Indicated F value (default 372) */
    uint8_t  di;                    /**< Indicated D value (default 1) */
    uint8_t  fmax_mhz;              /**< Max frequency supported by the card */
    uint8_t  guard_n;               /**< TC1: extra guard integer N (default 0) */
    bool     has_ta2;
    uint8_t  ta2;                   /**< Specific mode (cl. 8.3) */

    /* T=15 */
    bool     has_t15;
    uint8_t  x_clock_stop;          /**< 0=unsupported, 1=L, 2=H, 3=no preference (table 9) */
    uint8_t  y_class;               /**< Class mask: bit1=A, bit2=B, bit3=C (table 10) */
    bool     spu_used;
    bool     spu_standard;

    /* T=0 specific */
    uint8_t  wi;                    /**< TC2: waiting time integer (default 10) */

    /* T=1 specific (cl. 11.4) */
    bool     has_t1;
    uint8_t  ifsc;                  /**< Default 32 */
    uint8_t  cwi;                   /**< Default 13 */
    uint8_t  bwi;                   /**< Default 4 */
    bool     use_crc;               /**< First TC for T=1, bit 1 */

    uint8_t  historical[ISO7816_ATR_HIST_MAX];
    uint8_t  hist_len;

    bool     tck_present;
    bool     tck_ok;                /**< XOR of T0..TCK == 0 */
} iso7816_atr_t;

/**
 * @brief Parse a received ATR. buf holds the bytes from T0 on (TS passed
 * separately). len = number of bytes in buf (T0..TCK).
 * @param atr Receives the parsed ATR (zeroed first).
 * @param ts  Initial character TS.
 * @param buf Bytes from T0 on.
 * @param len Number of bytes in buf.
 * @return ISO7816_OK or an ATR status.
 */
iso7816_status_t iso7816_atr_parse(iso7816_atr_t *atr, uint8_t ts,
                                    const uint8_t *buf, size_t len);

/**
 * @brief Tell whether the received sequence (T0..current) is complete
 * (TCK included if required).
 * @param buf Bytes from T0 on.
 * @param len Number of bytes in buf.
 * @return true if the sequence is complete.
 */
bool iso7816_atr_complete(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
