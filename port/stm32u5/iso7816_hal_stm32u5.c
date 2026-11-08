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

static void vcc_select(stm32u5_ctx_t *port_ctx, iso7816_class_t cls)
{
    struct { GPIO_TypeDef *port; uint16_t pin; iso7816_class_t cls; } vcc_table[ISO7816_CLASS_COUNT];
    vcc_table[0] = (typeof(vcc_table[0])){ port_ctx->cfg.gpio_vcc_a, port_ctx->cfg.pin_vcc_a, ISO7816_CLASS_A };
    vcc_table[1] = (typeof(vcc_table[1])){ port_ctx->cfg.gpio_vcc_b, port_ctx->cfg.pin_vcc_b, ISO7816_CLASS_B };
    vcc_table[2] = (typeof(vcc_table[2])){ port_ctx->cfg.gpio_vcc_c, port_ctx->cfg.pin_vcc_c, ISO7816_CLASS_C };
    for (int i = 0; i < (int)ISO7816_CLASS_COUNT; i++) {
        if (vcc_table[i].port) {
            HAL_GPIO_WritePin(vcc_table[i].port, vcc_table[i].pin,
                              (vcc_table[i].cls == cls) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
}

static int p_init(void *ctx)
{
    stm32u5_ctx_t *port_ctx = ctx;
    /* I/O : open-drain, niveau haut (repos) ; RST à L ; VCC coupé */
    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Pin = port_ctx->cfg.pin_io;
    HAL_GPIO_Init(port_ctx->cfg.gpio_io, &gpio_init);
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_io, port_ctx->cfg.pin_io, GPIO_PIN_SET); /* ligne relâchée (H via pull-up) */

    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pin = port_ctx->cfg.pin_rst;
    HAL_GPIO_Init(port_ctx->cfg.gpio_rst, &gpio_init);
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_rst, port_ctx->cfg.pin_rst, GPIO_PIN_RESET);

    vcc_select(port_ctx, ISO7816_CLASS_A);
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_vcc_a ? port_ctx->cfg.gpio_vcc_a : port_ctx->cfg.gpio_rst,
                      port_ctx->cfg.gpio_vcc_a ? port_ctx->cfg.pin_vcc_a : port_ctx->cfg.pin_rst,
                      GPIO_PIN_RESET);

    /* base de temps 1 MHz */
    if (port_ctx->cfg.h_us) {
        uint32_t apb_clk_hz = HAL_RCC_GetPCLK1Freq();
        /* suppose TIM sur APB1 ; à ajuster si APB2 */
        port_ctx->cfg.h_us->Init.Prescaler = (apb_clk_hz / ISO7816_PORT_TIME_BASE_HZ) - 1U;
        port_ctx->cfg.h_us->Init.CounterMode = TIM_COUNTERMODE_UP;
        port_ctx->cfg.h_us->Init.Period = ISO7816_PORT_TIM_PERIOD;
        HAL_TIM_Base_Init(port_ctx->cfg.h_us);
        HAL_TIM_Base_Start(port_ctx->cfg.h_us);
    }
    return 0;
}

static void p_deinit(void *ctx) { (void)ctx; }

static void p_vcc_on(void *ctx, iso7816_class_t cls)
{
    stm32u5_ctx_t *port_ctx = ctx;
    vcc_select(port_ctx, cls);
}

static void p_vcc_off(void *ctx)
{
    stm32u5_ctx_t *port_ctx = ctx;
    if (port_ctx->cfg.gpio_vcc_a) HAL_GPIO_WritePin(port_ctx->cfg.gpio_vcc_a, port_ctx->cfg.pin_vcc_a, GPIO_PIN_RESET);
    if (port_ctx->cfg.gpio_vcc_b) HAL_GPIO_WritePin(port_ctx->cfg.gpio_vcc_b, port_ctx->cfg.pin_vcc_b, GPIO_PIN_RESET);
    if (port_ctx->cfg.gpio_vcc_c) HAL_GPIO_WritePin(port_ctx->cfg.gpio_vcc_c, port_ctx->cfg.pin_vcc_c, GPIO_PIN_RESET);
}

static void p_rst(void *ctx, bool high)
{
    stm32u5_ctx_t *port_ctx = ctx;
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_rst, port_ctx->cfg.pin_rst, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint32_t p_clk_start(void *ctx, uint32_t hz)
{
    stm32u5_ctx_t *port_ctx = ctx;
    if (!port_ctx->cfg.h_clk || hz == 0) return 0;
    uint32_t timer_clk_hz = HAL_RCC_GetPCLK1Freq();   /* ajuster si le TIM est sur APB2 */
    uint32_t prescaler = 0, autoreload;
    /* on vise un ARR petit pour la résolution : PSC = f_TIM/(hz*256) si possible */
    uint32_t target = hz * ISO7816_PORT_CLK_RES_FACTOR;
    if (target > timer_clk_hz) target = timer_clk_hz;
    prescaler = (timer_clk_hz / target) - 1U;
    autoreload = (timer_clk_hz / ((prescaler + 1U) * hz)) - 1U;
    port_ctx->tim_clk_hz = timer_clk_hz / ((prescaler + 1U) * (autoreload + 1U));
    __HAL_TIM_SET_PRESCALER(port_ctx->cfg.h_clk, prescaler);
    __HAL_TIM_SET_AUTORELOAD(port_ctx->cfg.h_clk, autoreload);
    __HAL_TIM_SET_COMPARE(port_ctx->cfg.h_clk, port_ctx->cfg.clk_channel,
                          (autoreload + 1U) / ISO7816_PORT_PWM_DUTY_DIV);  /* 50 % */
    HAL_TIM_PWM_Start(port_ctx->cfg.h_clk, port_ctx->cfg.clk_channel);
    return port_ctx->tim_clk_hz;
}

static void p_clk_stop(void *ctx, bool keep_high)
{
    stm32u5_ctx_t *port_ctx = ctx;
    if (port_ctx->cfg.h_clk) HAL_TIM_PWM_Stop(port_ctx->cfg.h_clk, port_ctx->cfg.clk_channel);
    /* force la broche CLK à l'état demandé (indicateur X, cl. 6.3.2) */
    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Pin = port_ctx->cfg.pin_clk;
    HAL_GPIO_Init(port_ctx->cfg.gpio_clk, &gpio_init);
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_clk, port_ctx->cfg.pin_clk, keep_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void p_io_tx(void *ctx, bool high)
{
    stm32u5_ctx_t *port_ctx = ctx;
    /* open-drain : écrire 1 relâche la ligne (H via pull-up), 0 tire à L */
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_io, port_ctx->cfg.pin_io, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void p_io_rx(void *ctx)
{
    stm32u5_ctx_t *port_ctx = ctx;
    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Pin = port_ctx->cfg.pin_io;
    HAL_GPIO_Init(port_ctx->cfg.gpio_io, &gpio_init);
}

static bool p_io_read(void *ctx)
{
    stm32u5_ctx_t *port_ctx = ctx;
    return HAL_GPIO_ReadPin(port_ctx->cfg.gpio_io, port_ctx->cfg.pin_io) == GPIO_PIN_SET;
}

static uint32_t p_micros(void *ctx)
{
    stm32u5_ctx_t *port_ctx = ctx;
    if (port_ctx->cfg.h_us) return __HAL_TIM_GET_COUNTER(port_ctx->cfg.h_us);
    return HAL_GetTick() * ISO7816_US_PER_MS;
}

static void p_delay_us(void *ctx, uint32_t us)
{
    uint32_t start_us = p_micros(ctx);
    while ((uint32_t)(p_micros(ctx) - start_us) < us) {
        /* busy wait ; le timer 1 MHz a une résolution de 1 µs */
    }
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
