/*
 * iso7816_hal_stm32u5.c — implémentation STM32U5 de iso7816_hal_t.
 *
 * Non compilé par défaut (option CMake ISO7816_BUILD_STM32U5_PORT=ON,
 * à activer dans un projet STM32Cube avec le HAL STM32U5 disponible).
 */
#include "iso7816_hal_stm32u5.h"

#include <string.h>

typedef struct {
    iso7816_hal_stm32u5_config_t cfg;
    iso7816_hal_t hal;
    uint32_t tim_clk_hz;   /* horloge du timer CLK (f après PSC) */
} stm32u5_ctx_t;

static stm32u5_ctx_t g_ctx;

static void vcc_select(stm32u5_ctx_t *c, iso7816_class_t cls)
{
    struct { GPIO_TypeDef *port; uint16_t pin; iso7816_class_t cls; } tab[3];
    tab[0] = (typeof(tab[0])){ c->cfg.gpio_vcc_a, c->cfg.pin_vcc_a, ISO7816_CLASS_A };
    tab[1] = (typeof(tab[1])){ c->cfg.gpio_vcc_b, c->cfg.pin_vcc_b, ISO7816_CLASS_B };
    tab[2] = (typeof(tab[2])){ c->cfg.gpio_vcc_c, c->cfg.pin_vcc_c, ISO7816_CLASS_C };
    for (int i = 0; i < 3; i++) {
        if (tab[i].port) {
            HAL_GPIO_WritePin(tab[i].port, tab[i].pin, (tab[i].cls == cls) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
}

static int p_init(void *ctx)
{
    stm32u5_ctx_t *c = ctx;
    /* I/O : open-drain, niveau haut (repos) ; RST à L ; VCC coupé */
    GPIO_InitTypeDef g = { 0 };
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Pin = c->cfg.pin_io;
    HAL_GPIO_Init(c->cfg.gpio_io, &g);
    HAL_GPIO_WritePin(c->cfg.gpio_io, c->cfg.pin_io, GPIO_PIN_SET);   /* ligne relâchée (H via pull-up) */

    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pin = c->cfg.pin_rst;
    HAL_GPIO_Init(c->cfg.gpio_rst, &g);
    HAL_GPIO_WritePin(c->cfg.gpio_rst, c->cfg.pin_rst, GPIO_PIN_RESET);

    vcc_select(c, ISO7816_CLASS_A);
    HAL_GPIO_WritePin(c->cfg.gpio_vcc_a ? c->cfg.gpio_vcc_a : c->cfg.gpio_rst,
                      c->cfg.gpio_vcc_a ? c->cfg.pin_vcc_a : c->cfg.pin_rst, GPIO_PIN_RESET);

    /* base de temps 1 MHz */
    if (c->cfg.h_us) {
        uint32_t apb = HAL_RCC_GetPCLK1Freq();
        /* suppose TIM sur APB1 ; à ajuster si APB2 */
        c->cfg.h_us->Init.Prescaler = (apb / 1000000U) - 1U;
        c->cfg.h_us->Init.CounterMode = TIM_COUNTERMODE_UP;
        c->cfg.h_us->Init.Period = 0xFFFFU;
        HAL_TIM_Base_Init(c->cfg.h_us);
        HAL_TIM_Base_Start(c->cfg.h_us);
    }
    return 0;
}

static void p_deinit(void *ctx) { (void)ctx; }

static void p_vcc_on(void *ctx, iso7816_class_t cls)
{
    stm32u5_ctx_t *c = ctx;
    vcc_select(c, cls);
}

static void p_vcc_off(void *ctx)
{
    stm32u5_ctx_t *c = ctx;
    if (c->cfg.gpio_vcc_a) HAL_GPIO_WritePin(c->cfg.gpio_vcc_a, c->cfg.pin_vcc_a, GPIO_PIN_RESET);
    if (c->cfg.gpio_vcc_b) HAL_GPIO_WritePin(c->cfg.gpio_vcc_b, c->cfg.pin_vcc_b, GPIO_PIN_RESET);
    if (c->cfg.gpio_vcc_c) HAL_GPIO_WritePin(c->cfg.gpio_vcc_c, c->cfg.pin_vcc_c, GPIO_PIN_RESET);
}

static void p_rst(void *ctx, bool high)
{
    stm32u5_ctx_t *c = ctx;
    HAL_GPIO_WritePin(c->cfg.gpio_rst, c->cfg.pin_rst, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint32_t p_clk_start(void *ctx, uint32_t hz)
{
    stm32u5_ctx_t *c = ctx;
    if (!c->cfg.h_clk || hz == 0) return 0;
    uint32_t tim_hz = HAL_RCC_GetPCLK1Freq();   /* ajuster si le TIM est sur APB2 */
    uint32_t psc = 0, arr;
    /* on vise arr petit pour la résolution : PSC = tim_hz/(hz*256) si possible */
    uint32_t target = hz * 256U;
    if (target > tim_hz) target = tim_hz;
    psc = (tim_hz / target) - 1U;
    arr = (tim_hz / ((psc + 1U) * hz)) - 1U;
    c->tim_clk_hz = tim_hz / ((psc + 1U) * (arr + 1U));
    __HAL_TIM_SET_PRESCALER(c->cfg.h_clk, psc);
    __HAL_TIM_SET_AUTORELOAD(c->cfg.h_clk, arr);
    __HAL_TIM_SET_COMPARE(c->cfg.h_clk, c->cfg.clk_channel, (arr + 1U) / 2U);  /* 50 % */
    HAL_TIM_PWM_Start(c->cfg.h_clk, c->cfg.clk_channel);
    return c->tim_clk_hz;
}

static void p_clk_stop(void *ctx, bool keep_high)
{
    stm32u5_ctx_t *c = ctx;
    if (c->cfg.h_clk) HAL_TIM_PWM_Stop(c->cfg.h_clk, c->cfg.clk_channel);
    /* force la broche CLK à l'état demandé (indicateur X, cl. 6.3.2) */
    GPIO_InitTypeDef g = { 0 };
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = c->cfg.pin_clk;
    HAL_GPIO_Init(c->cfg.gpio_clk, &g);
    HAL_GPIO_WritePin(c->cfg.gpio_clk, c->cfg.pin_clk, keep_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void p_io_tx(void *ctx, bool high)
{
    stm32u5_ctx_t *c = ctx;
    /* open-drain : écrire 1 relâche la ligne (H via pull-up), 0 tire à L */
    HAL_GPIO_WritePin(c->cfg.gpio_io, c->cfg.pin_io, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void p_io_rx(void *ctx)
{
    stm32u5_ctx_t *c = ctx;
    GPIO_InitTypeDef g = { 0 };
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    g.Pin = c->cfg.pin_io;
    HAL_GPIO_Init(c->cfg.gpio_io, &g);
}

static bool p_io_read(void *ctx)
{
    stm32u5_ctx_t *c = ctx;
    return HAL_GPIO_ReadPin(c->cfg.gpio_io, c->cfg.pin_io) == GPIO_PIN_SET;
}

static uint32_t p_micros(void *ctx)
{
    stm32u5_ctx_t *c = ctx;
    if (c->cfg.h_us) return __HAL_TIM_GET_COUNTER(c->cfg.h_us);
    return HAL_GetTick() * 1000U;
}

static void p_delay_us(void *ctx, uint32_t us)
{
    stm32u5_ctx_t *c = ctx;
    uint32_t start = p_micros(ctx);
    while ((uint32_t)(p_micros(ctx) - start) < us) {
        /* busy wait ; le timer 1 MHz a une résolution de 1 µs */
    }
    (void)c;
}

const iso7816_hal_t *iso7816_hal_stm32u5(const iso7816_hal_stm32u5_config_t *cfg)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *cfg;
    g_ctx.hal.ctx = &g_ctx;
    g_ctx.hal.init = p_init;
    g_ctx.hal.deinit = p_deinit;
    g_ctx.hal.vcc_on = p_vcc_on;
    g_ctx.hal.vcc_off = p_vcc_off;
    g_ctx.hal.rst_drive = p_rst;
    g_ctx.hal.clk_start = p_clk_start;
    g_ctx.hal.clk_stop = p_clk_stop;
    g_ctx.hal.io_tx = p_io_tx;
    g_ctx.hal.io_rx = p_io_rx;
    g_ctx.hal.io_read = p_io_read;
    g_ctx.hal.micros = p_micros;
    g_ctx.hal.delay_us = p_delay_us;
    return &g_ctx.hal;
}