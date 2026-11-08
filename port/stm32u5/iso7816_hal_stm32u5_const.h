/*
 * @file iso7816_hal_stm32u5_const.h
 * @brief STM32U5 port constants (kept out of iso7816_const.h, which only
 * holds norm constants).
 */
#pragma once

#define ISO7816_PORT_TIME_BASE_HZ   1000000U /* µs time base: TIM at 1 MHz */
#define ISO7816_PORT_TIM_PERIOD     0xFFFFU  /* max period of the time-base timer */
#define ISO7816_PORT_CLK_RES_FACTOR 256U     /* PSC = f_TIM / (hz * 256) to keep ARR small */
#define ISO7816_PORT_PWM_DUTY_DIV   2U       /* 50 % duty cycle */
#define ISO7816_PORT_IO_PULLUP_OHM  20000U   /* external I/O pull-up = 20 kΩ to VCC (cl. 5.2.5) */
