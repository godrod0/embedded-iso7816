/**
 * @file iso7816_hal.h
 * @brief Unique hardware access API (Hardware Abstraction Layer).
 *
 * The whole library goes exclusively through this function-pointer struct,
 * injected into iso7816_init(). Two ports:
 *   - port/stm32u5/  : STM32U5 USART in smartcard mode (HAL SMARTCARD):
 *                      the peripheral owns the I/O line (TX, open-drain) and
 *                      generates the card clock (CK); character frame
 *                      (start + 8 bits + parity), NACK and guard time are
 *                      handled in hardware.
 *   - tests/pc_card  : PC mock for unit/functional tests.
 *
 * Pin mapping (ISO/IEC 7816-3, cl. 5.2):
 *   C1 VCC : supply (class A=5V, B=3V, C=1.8V) — vcc_on/vcc_off
 *   C2 RST : reset (H/L) — rst_drive
 *   C3 CLK : clock — clk_start/clk_stop (smartcard peripheral CK)
 *   C5 GND : ground (port reference)
 *   C6 SPU : not driven here
 *   C7 I/O : half-duplex serial line — tx_byte/rx_byte (smartcard TX);
 *            io_read to sense the line (answer detection, clock stop);
 *            io_drive to force the line (deactivation, cl. 6.4)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "iso7816_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Operating classes (cl. 5.1.3). The value is the supply voltage. */
typedef enum {
    ISO7816_CLASS_A = 5,    /**< 5 V   */
    ISO7816_CLASS_B = 3,    /**< 3 V   */
    ISO7816_CLASS_C = 1,    /**< 1.8 V */
} iso7816_class_t;

/** @brief Hardware abstraction layer: all hardware access goes through these callbacks. */
typedef struct iso7816_hal {
    void *ctx;      /**< Opaque context, passed back to every callback */

    int      (*init)(void *ctx);                        /**< Peripheral init (GPIO, USART, timers, power) */
    void     (*deinit)(void *ctx);                      /**< Peripheral deinit */

    void     (*vcc_on)(void *ctx, iso7816_class_t cls); /**< Power C1 according to the class */
    void     (*vcc_off)(void *ctx);                     /**< Cut card power */

    void     (*rst_drive)(void *ctx, bool high);        /**< C2: H/L level */

    uint32_t (*clk_start)(void *ctx, uint32_t hz);      /**< Start C3; returns the actual frequency */
    void     (*clk_stop)(void *ctx, bool keep_high);    /**< Clock stop (indicator X, cl. 6.3.2) */

    /**
     * @brief Transmit one character (full frame: start + 8 bits + even parity).
     * The port handles the frame and the guard time.
     * @return ISO7816_OK, or ISO7816_CHAR_PARITY if the error signal (NACK)
     *         was received during the guard time (cl. 7.3).
     */
    iso7816_status_t (*tx_byte)(void *ctx, uint8_t byte);

    /**
     * @brief Receive one character (full frame).
     * @param timeout_us Waiting time (WT) in µs.
     * @return ISO7816_OK, ISO7816_CHAR_WT if the sender is silent,
     *         ISO7816_CHAR_PARITY if parity is wrong (the port sent the
     *         error signal / NACK, cl. 7.3).
     */
    iso7816_status_t (*rx_byte)(void *ctx, uint8_t *byte, uint32_t timeout_us);

    bool     (*io_read)(void *ctx);                     /**< Read the C7 line level */
    void     (*io_drive)(void *ctx, bool high);         /**< Force C7 as output H/L (deactivation) */

    uint32_t (*micros)(void *ctx);                      /**< Time base in µs (BGT, waits) */
    void     (*delay_us)(void *ctx, uint32_t us);       /**< Blocking sleep */
} iso7816_hal_t;

#ifdef __cplusplus
}
#endif
