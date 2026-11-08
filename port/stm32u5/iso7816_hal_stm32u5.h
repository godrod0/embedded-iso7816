/*
 * iso7816_hal_stm32u5.h — port STM32U5 de iso7816_hal_t.
 *
 * Basé sur la bibliothèque SMARTCARD de la HAL STM32 (stm32u5xx_hal_smartcard.h) :
 * le USART en mode smartcard pilote la ligne I/O (broche TX, open-drain) et
 * génère l'horloge carte (broche CK) ; la trame caractère (start + 8 bits +
 * parité paire), le NACK et le guard time sont gérés par le périphérique.
 *
 * Brochage type (à adapter dans la config) :
 *   C1 VCC : sélection de classe par 3 load-switches (EN_5V / EN_3V / EN_1V8)
 *            pilotés en GPIO
 *   C2 RST : GPIO push-pull
 *   C3 CLK : CK du USART (AF) ; basculable en GPIO pour le clock stop (cl. 6.3.2)
 *   C5 GND : masse
 *   C7 I/O : TX du USART en mode smartcard (open-drain + pull-up externe
 *            ISO7816_PORT_IO_PULLUP_OHM vers VCC, cl. 5.2.5)
 *
 * Base de temps µs : TIM libre cadencé à 1 MHz (ISO7816_PORT_TIME_BASE_HZ).
 *
 * Nécessite stm32u5xx_hal_smartcard.c dans le projet (HAL STM32U5).
 */
#pragma once

#include "stm32u5xx_hal.h"

#include "iso7816/iso7816_const.h"
#include "iso7816/iso7816_hal.h"

#include "iso7816_hal_stm32u5_const.h"   /* constantes propres au port STM32U5 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* alimentation VCC : un GPIO d'activation par classe (0 = non câblé) */
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

/* Remplit *hal avec les callbacks STM32U5 (smartcard) ; ctx = config interne alloué en statique. */
const iso7816_hal_t *iso7816_hal_stm32u5(const iso7816_hal_stm32u5_config_t *cfg);

#ifdef __cplusplus
}
#endif
