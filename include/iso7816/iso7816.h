/**
 * @file iso7816.h
 * @brief Main API of the ISO/IEC 7816-3:2006 library (IFD / reader side).
 *
 * Typical sequence (cl. 6.1):
 *   iso7816_init() -> iso7816_activate() -> [iso7816_pps()] -> iso7816_exchange()* -> iso7816_deactivate()
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iso7816_atr.h"
#include "iso7816_hal.h"
#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Library configuration passed to iso7816_init(). */
typedef struct {
    const iso7816_hal_t *hal;   /**< Hardware API (mandatory) */
    uint32_t clk_hz;            /**< Clock during activation/reset/ATR: 1..5 MHz (default 1 MHz) */
    uint32_t timeout_ms;        /**< Fallback global timeout (default 1000 ms) */
    uint8_t  max_retries;       /**< Max character retransmissions (cl. 7.3, default 3) */
} iso7816_config_t;

/** @brief Library context (device / IFD state). */
typedef struct iso7816 {
    const iso7816_hal_t *hal;

    iso7816_class_t cls;        /**< Applied operating class */
    uint32_t f_hz;              /**< Current CLK frequency */
    uint16_t F;                 /**< Current clock-rate conversion integer */
    uint8_t  D;                 /**< Current bit-rate adjustment integer */
    bool     inverse;           /**< Coding convention (from TS) */
    uint8_t  guard_n;           /**< TC1: extra guard time N */
    bool     has_t15;
    uint32_t wt_us;             /**< Current waiting time in µs */
    uint32_t timeout_ms;
    uint8_t  max_retries;
    bool     active;            /**< Card activated (ATR received) */
    uint8_t  protocol;          /**< Selected protocol (T=0 or T=1) */
    bool     use_error_signal;  /**< Error signal / retransmission (cl. 7.3): yes except T=1 */

    iso7816_atr_t atr;

    /* T=0 (cl. 10.2) */
    uint8_t  wi;

    /* T=1 (cl. 11.4) */
    uint8_t  ifsc, ifsd;
    uint8_t  cwi, bwi;
    bool     use_crc;
    uint8_t  ns_ifd;            /**< Next N(S) for IFD I-blocks */
    uint8_t  ns_card;           /**< Last N(S) received from the card */

    uint32_t last_tx_lead_us;   /**< Leading edge of last transmitted character (GT) */
    uint32_t last_rx_lead_us;   /**< Leading edge of last received character (BGT, T=1) */
    bool     last_dir_rx;       /**< Last direction was receive (BGT) */
    bool     clock_stopped;     /**< Clock stopped (cl. 6.3.2) */
    uint32_t clk_reset_hz;      /**< Clock applied during activation/reset/ATR */
    uint32_t atr_end_us;        /**< End of last ATR reception */
} iso7816_t;

/**
 * @brief Initialize the library context and the hardware port.
 * @param dev  Context to initialize (zeroed first).
 * @param hal  Hardware API (mandatory).
 * @param cfg  Optional configuration (NULL = defaults).
 * @return ISO7816_OK, ISO7816_GEN_PARAM or ISO7816_GEN_HAL.
 */
iso7816_status_t iso7816_init(iso7816_t *dev, const iso7816_hal_t *hal,
                              const iso7816_config_t *cfg);

/**
 * @brief Activation + cold reset + ATR (cl. 6.2.1, 6.2.2, 8). Selects the class.
 * @param dev  Library context.
 * @param cls  Requested operating class (A/B/C); other classes are tried in turn (cl. 6.2.4).
 * @return ISO7816_OK or a DEV/GEN/ATR/CHAR status.
 */
iso7816_status_t iso7816_activate(iso7816_t *dev, iso7816_class_t cls);

/**
 * @brief Warm reset + new ATR (cl. 6.2.3).
 * @param dev  Library context (must be active).
 * @return ISO7816_OK or a DEV/GEN/ATR status.
 */
iso7816_status_t iso7816_warm_reset(iso7816_t *dev);

/**
 * @brief Optional PPS (cl. 9) — see iso7816_pps_exchange().
 * @param dev      Library context.
 * @param protocol Requested protocol T (0 or 1).
 * @param F        Requested clock-rate integer.
 * @param D        Requested bit-rate integer.
 * @return ISO7816_OK or ISO7816_PPS_* / GEN status.
 */
iso7816_status_t iso7816_pps(iso7816_t *dev, uint8_t protocol, uint16_t F, uint8_t D);

/**
 * @brief Exchange a command-response pair (cl. 12) over the selected protocol:
 * command APDU -> response APDU (data + SW1-SW2).
 * @param dev       Library context (must be active).
 * @param cmd       Command APDU buffer.
 * @param cmd_len   Command length in bytes.
 * @param resp      Response data buffer (without SW1-SW2).
 * @param resp_cap  Response buffer capacity.
 * @param resp_len  Receives the response data length (without SW1-SW2).
 * @param sw        Receives SW1-SW2 (e.g. 0x9000). An error SW is not a transport
 *                  error: the function returns ISO7816_OK with *sw != 0x9000.
 * @return Transport status (ISO7816_OK on success at transport level).
 */
iso7816_status_t iso7816_exchange(iso7816_t *dev,
                                  const uint8_t *cmd, size_t cmd_len,
                                  uint8_t *resp, size_t resp_cap,
                                  size_t *resp_len, uint16_t *sw);

/**
 * @brief Stop / restart the clock if supported (cl. 6.3.2, indicator X).
 */
iso7816_status_t iso7816_clock_stop(iso7816_t *dev);
iso7816_status_t iso7816_clock_restart(iso7816_t *dev);

/**
 * @brief Deactivation (cl. 6.4): RST=L, CLK=L, I/O=L, VCC off.
 */
iso7816_status_t iso7816_deactivate(iso7816_t *dev);

/** @brief Read-only access to the parsed ATR (NULL if dev is NULL). */
const iso7816_atr_t *iso7816_get_atr(const iso7816_t *dev);

/** @brief Selected protocol (T=0/T=1), or ISO7816_PROTOCOL_NONE if dev is NULL. */
uint8_t iso7816_get_protocol(const iso7816_t *dev);

#ifdef __cplusplus
}
#endif
