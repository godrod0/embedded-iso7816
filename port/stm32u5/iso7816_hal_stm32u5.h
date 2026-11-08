/*
 * iso7816_hal_stm32u5.h — port STM32U5 de l'API iso7816_hal_t.
 *
 * Brochage type (à adapter dans la config) :
 *   C1 VCC : sélection de classe par 3 load-switches (EN_5V / EN_3V / EN_1V8)
 *            pilotés en GPIO (ou PMIC externe via callback)
 *   C2 RST : GPIO push-pull
 *   C3 CLK : TIMx CHx en PWM 50 % (fréquence = f_TIM / ((PSC+1)(ARR+1)))
 *   C5 GND : masse
 *   C7 I/O : GPIO open-drain + pull-up externe 20 kΩ vers VCC (cl. 5.2.5) ;
 *            en réception : entrée flottante
 *
 * Base de temps µs : TIMy libre cadencé à 1 MHz (PSC = f_APB/1e6 - 1).
 */
#pragma once

#include "stm32u5xx_hal.h"

#include "iso7816/iso7816_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* alimentation VCC : un GPIO d'activation par classe (0 = non câblé) */
    GPIO_TypeDef *gpio_vcc_a;  uint16_t pin_vcc_a;    /* 5 V   */
    GPIO_TypeDef *gpio_vcc_b;  uint16_t pin_vcc_b;    /* 3 V   */
    GPIO_TypeDef *gpio_vcc_c;  uint16_t pin_vcc_c;    /* 1,8 V */

    GPIO_TypeDef *gpio_rst;    uint16_t pin_rst;

    TIM_HandleTypeDef *h_clk;            /* PWM sur un canal (ex. TIM2 CH1) */
    uint32_t           clk_channel;      /* HAL_TIM_ACTIVE_CHANNEL_x */
    GPIO_TypeDef      *gpio_clk;         /* pour forcer H/L à l'arrêt */
    uint16_t           pin_clk;

    GPIO_TypeDef *gpio_io;     uint16_t pin_io;

    TIM_HandleTypeDef *h_us;             /* base de temps 1 MHz (ex. TIM3) */
} iso7816_hal_stm32u5_config_t;

/* Remplit *hal avec les callbacks STM32U5 ; ctx = config interne alloué en statique. */
const iso7816_hal_t *iso7816_hal_stm32u5(const iso7816_hal_stm32u5_config_t *cfg);

#ifdef __cplusplus
}
#endif