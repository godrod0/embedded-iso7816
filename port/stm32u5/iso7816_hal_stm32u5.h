/**
 * @file iso7816_hal_stm32u5.h
 * @brief STM32U5 port of iso7816_hal_t.
 *
 * Based on the STM32 HAL SMARTCARD library (stm32u5xx_hal_smartcard.h):
 * the USART in smartcard mode drives the I/O line (TX pin, open-drain) and
 * generates the card clock (CK pin); the character frame (start + 8 bits +
 * even parity), NACK and guard time are handled by the peripheral.
 *
 * Typical wiring (adapt in the config):
 *   C1 VCC : class selection via 3 load-switches (EN_5V / EN_3V / EN_1V8)
 *            driven as GPIO
 *   C2 RST : GPIO push-pull
 *   C3 CLK : USART CK (AF); switchable to GPIO for clock stop (cl. 6.3.2)
 *   C5 GND : ground
 *   C7 I/O : USART TX in smartcard mode (open-drain + external pull-up
 *            ISO7816_PORT_IO_PULLUP_OHM to VCC, cl. 5.2.5)
 *
 * µs time base: free-running TIM at 1 MHz (ISO7816_PORT_TIME_BASE_HZ).
 *
 * Requires stm32u5xx_hal_smartcard.c in the project (STM32U5 HAL).
 */
#pragma once

#include "stm32u5xx_hal.h"

#include "iso7816/iso7816_const.h"
#include "iso7816/iso7816_hal.h"

#include "iso7816_hal_stm32u5_const.h"   /* constants specific to the STM32U5 port */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* VCC supply: one enable GPIO per class (0 = not wired) */
    GPIO_TypeDef *gpio_vcc_a;  uint16_t pin_vcc_a;    /* 5 V   */
    GPIO_TypeDef *gpio_vcc_b;  uint16_t pin_vcc_b;    /* 3 V   */
    GPIO_TypeDef *gpio_vcc_c;  uint16_t pin_vcc_c;    /* 1,8 V */

    GPIO_TypeDef *gpio_rst;    uint16_t pin_rst;

    /* USART en mode smartcard : C7 I/O = TX (AF open-drain), C3 CLK = CK (AF) */
    USART_TypeDef *usart;              /* instance (ex. USART1) */
    GPIO_TypeDef  *gpio_tx;  uint16_t pin_tx;
    GPIO_TypeDef  *gpio_ck;  uint16_t pin_ck;
    uint8_t        gpio_af;            /* alternate function commune TX/CK */

    TIM_HandleTypeDef *h_us;           /* base de temps 1 MHz (ex. TIM3) */
} iso7816_hal_stm32u5_config_t;

/**
 * @brief Fills *hal with the STM32U5 (smartcard) callbacks.
 * @param cfg Port configuration (copied into a static internal context).
 * @return Pointer to the filled HAL structure.
 */
const iso7816_hal_t *iso7816_hal_stm32u5(const iso7816_hal_stm32u5_config_t *cfg);

#ifdef __cplusplus
}
#endif
