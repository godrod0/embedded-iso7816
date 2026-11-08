/*
 * iso7816_hal_stm32u5_const.h — constantes du port STM32U5 (dédié au port,
 * hors de iso7816_const.h qui ne contient que les constantes de la norme).
 */
#pragma once

#define ISO7816_PORT_TIME_BASE_HZ   1000000U /* base de temps µs : TIM à 1 MHz */
#define ISO7816_PORT_TIM_PERIOD     0xFFFFU  /* période max du timer de base de temps */
#define ISO7816_PORT_CLK_RES_FACTOR 256U     /* PSC = f_TIM / (hz * 256) pour ARR petit */
#define ISO7816_PORT_PWM_DUTY_DIV   2U       /* rapport cyclique 50 % */
#define ISO7816_PORT_IO_PULLUP_OHM  20000U   /* pull-up externe I/O = 20 kΩ vers VCC (cl. 5.2.5) */
