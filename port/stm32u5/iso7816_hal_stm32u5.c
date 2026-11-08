/**
 * @file iso7816_hal_stm32u5.c
 * @brief STM32U5 implementation of iso7816_hal_t, based on the STM32 HAL
 * SMARTCARD library (stm32u5xx_hal_smartcard.h).
 *
 * The USART in smartcard mode handles:
 *   - C7 I/O on TX (open-drain): full frames via HAL_SMARTCARD_Transmit/Receive,
 *     even parity + automatic NACK (cl. 7.3) and guard time (USART_GTPR) in hardware;
 *   - C3 CLK on CK: frequency = f_USART / (2 * Prescaler).
 *
 * Not built by default (CMake option ISO7816_BUILD_STM32U5_PORT=ON, to enable
 * in an STM32Cube project with the STM32U5 HAL available, including
 * stm32u5xx_hal_smartcard.c).
 */
#include "iso7816_hal_stm32u5.h"

#include <string.h>

typedef struct {
    iso7816_hal_stm32u5_config_t cfg;
    iso7816_hal_t hal;
    SMARTCARD_HandleTypeDef hsc;
    uint32_t usart_clk_hz;   /* horloge de l'USART (APB) */
    uint32_t clk_hz;         /* current CK frequency */
} stm32u5_ctx_t;

static stm32u5_ctx_t g_ctx;

/* ---------- power / reset (GPIO, unchanged) ---------- */

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

static void p_vcc_on(void *ctx, iso7816_class_t cls)
{
    vcc_select(ctx, cls);
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

/* ---------- USART smartcard ---------- */

/* Restores TX to AF open-drain (smartcard mode) if the pin was switched to GPIO. */
static void tx_ensure_af(stm32u5_ctx_t *port_ctx)
{
    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = port_ctx->cfg.gpio_af;
    gpio_init.Pin = port_ctx->cfg.pin_tx;
    HAL_GPIO_Init(port_ctx->cfg.gpio_tx, &gpio_init);
}

/* HAL callback invoked by HAL_SMARTCARD_Init: USART clock + GPIO AF TX/CK. */
void HAL_SMARTCARD_MspInit(SMARTCARD_HandleTypeDef *hsc)
{
    (void)hsc;
    stm32u5_ctx_t *port_ctx = &g_ctx;

#if defined(USART1)
    if (port_ctx->cfg.usart == USART1) __HAL_RCC_USART1_CLK_ENABLE();
#endif
#if defined(USART2)
    if (port_ctx->cfg.usart == USART2) __HAL_RCC_USART2_CLK_ENABLE();
#endif
#if defined(USART3)
    if (port_ctx->cfg.usart == USART3) __HAL_RCC_USART3_CLK_ENABLE();
#endif
#if defined(UART4)
    if (port_ctx->cfg.usart == UART4) __HAL_RCC_UART4_CLK_ENABLE();
#endif
#if defined(UART5)
    if (port_ctx->cfg.usart == UART5) __HAL_RCC_UART5_CLK_ENABLE();
#endif

    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = port_ctx->cfg.gpio_af;

    gpio_init.Mode = GPIO_MODE_AF_OD;   /* TX = C7 I/O, open-drain (mode smartcard) */
    gpio_init.Pin = port_ctx->cfg.pin_tx;
    HAL_GPIO_Init(port_ctx->cfg.gpio_tx, &gpio_init);

    gpio_init.Mode = GPIO_MODE_AF_PP;   /* CK = C3 CLK */
    gpio_init.Pin = port_ctx->cfg.pin_ck;
    HAL_GPIO_Init(port_ctx->cfg.gpio_ck, &gpio_init);
}

static int p_init(void *ctx)
{
    stm32u5_ctx_t *port_ctx = ctx;

    /* RST low, VCC off */
    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Pin = port_ctx->cfg.pin_rst;
    HAL_GPIO_Init(port_ctx->cfg.gpio_rst, &gpio_init);
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_rst, port_ctx->cfg.pin_rst, GPIO_PIN_RESET);
    vcc_select(port_ctx, ISO7816_CLASS_A);
    p_vcc_off(ctx);

    /* USART in smartcard mode: 9-bit frame (8 data + even parity), 1.5 stop bits */
    port_ctx->hsc.Instance = port_ctx->cfg.usart;
    port_ctx->hsc.Init.WordLength = SMARTCARD_WORDLENGTH_9B;
    port_ctx->hsc.Init.StopBits = SMARTCARD_STOPBITS_1_5;
    port_ctx->hsc.Init.Parity = SMARTCARD_PARITY_EVEN;
    port_ctx->hsc.Init.Mode = SMARTCARD_MODE_TX_RX;
    port_ctx->hsc.Init.CLKPolarity = SMARTCARD_CLOCKPOLARITY_LOW;
    port_ctx->hsc.Init.CLKPhase = SMARTCARD_PHASE_1EDGE;
    port_ctx->hsc.Init.CLKLastBit = SMARTCARD_LASTBIT_DISABLE;
    port_ctx->hsc.Init.Prescaler = 1;
    port_ctx->hsc.Init.GuardTime = ISO7816_CHAR_GT_ETU;   /* guard time de base (cl. 7.2) */
    port_ctx->hsc.Init.NACKState = SMARTCARD_NACK_ENABLED; /* signal d'erreur/NACK auto (cl. 7.3) */
    if (HAL_SMARTCARD_Init(&port_ctx->hsc) != HAL_OK) return -1;
    __HAL_SMARTCARD_ENABLE(&port_ctx->hsc);

    /* horloge de l'USART (APB) pour le calcul du prescaler de CK :
       USART1/USART6 on APB2, others on APB1 (adjust per model) */
#if defined(USART6)
    if (port_ctx->cfg.usart == USART1 || port_ctx->cfg.usart == USART6)
        port_ctx->usart_clk_hz = HAL_RCC_GetPCLK2Freq();
    else
#endif
#if defined(USART1)
    if (port_ctx->cfg.usart == USART1)
        port_ctx->usart_clk_hz = HAL_RCC_GetPCLK2Freq();
    else
#endif
        port_ctx->usart_clk_hz = HAL_RCC_GetPCLK1Freq();

    /* base de temps 1 MHz */
    if (port_ctx->cfg.h_us) {
        uint32_t apb_clk_hz = HAL_RCC_GetPCLK1Freq();
        /* assume TIM on APB1; adjust if APB2 */
        port_ctx->cfg.h_us->Init.Prescaler = (apb_clk_hz / ISO7816_PORT_TIME_BASE_HZ) - 1U;
        port_ctx->cfg.h_us->Init.CounterMode = TIM_COUNTERMODE_UP;
        port_ctx->cfg.h_us->Init.Period = ISO7816_PORT_TIM_PERIOD;
        HAL_TIM_Base_Init(port_ctx->cfg.h_us);
        HAL_TIM_Base_Start(port_ctx->cfg.h_us);
    }
    return 0;
}

static void p_deinit(void *ctx) { (void)ctx; }

/* C3 CLK: CK = f_USART / (2 * Prescaler). Returns the actual frequency. */
static uint32_t p_clk_start(void *ctx, uint32_t hz)
{
    stm32u5_ctx_t *port_ctx = ctx;
    if (!port_ctx->cfg.usart || hz == 0) return 0;

    /* CK back to AF (after a possible clock stop in GPIO) */
    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = port_ctx->cfg.gpio_af;
    gpio_init.Pin = port_ctx->cfg.pin_ck;
    HAL_GPIO_Init(port_ctx->cfg.gpio_ck, &gpio_init);

    uint32_t prescaler = port_ctx->usart_clk_hz / (2U * hz);
    if (prescaler < 1U) prescaler = 1U;
    if (prescaler > 31U) prescaler = 31U;   /* champ PRESC : 5 bits effectifs */
    port_ctx->hsc.Init.Prescaler = prescaler;
    MODIFY_REG(port_ctx->hsc.Instance->PRESC, USART_PRESC_PRESCALER,
               prescaler << USART_PRESC_PRESCALER_Pos);
    __HAL_SMARTCARD_ENABLE(&port_ctx->hsc);

    port_ctx->clk_hz = port_ctx->usart_clk_hz / (2U * prescaler);
    return port_ctx->clk_hz;
}

/* Clock stop (cl. 6.3.2): CK switched to GPIO and forced to the requested level. */
static void p_clk_stop(void *ctx, bool keep_high)
{
    stm32u5_ctx_t *port_ctx = ctx;
    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Pin = port_ctx->cfg.pin_ck;
    HAL_GPIO_Init(port_ctx->cfg.gpio_ck, &gpio_init);
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_ck, port_ctx->cfg.pin_ck,
                      keep_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Transmits a full frame (start + 8 bits + parity) via the peripheral.
   Returns ISO7816_CHAR_PARITY if the card sent the error signal (NACK)
   during the guard time (cl. 7.3). */
static iso7816_status_t p_tx_byte(void *ctx, uint8_t byte)
{
    stm32u5_ctx_t *port_ctx = ctx;
    tx_ensure_af(port_ctx);
    HAL_StatusTypeDef hal_stat =
        HAL_SMARTCARD_Transmit(&port_ctx->hsc, &byte, 1, ISO7816_DEFAULT_TIMEOUT_MS);
    if (__HAL_SMARTCARD_GET_FLAG(&port_ctx->hsc, SMARTCARD_FLAG_PERR) ||
        (HAL_SMARTCARD_GetError(&port_ctx->hsc) & SMARTCARD_ERROR_PE)) {
        __HAL_SMARTCARD_CLEAR_PEFLAG(&port_ctx->hsc);
        return ISO7816_CHAR_PARITY;   /* NACK received: repeat the character (cl. 7.3) */
    }
    return (hal_stat == HAL_OK) ? ISO7816_OK : ISO7816_GEN_HAL;
}

/* Receives a full frame. timeout_us = WT ;
   wrong parity -> the peripheral automatically sends the NACK and returns
   ISO7816_CHAR_PARITY. */
static iso7816_status_t p_rx_byte(void *ctx, uint8_t *byte, uint32_t timeout_us)
{
    stm32u5_ctx_t *port_ctx = ctx;
    tx_ensure_af(port_ctx);
    uint32_t timeout_ms = (timeout_us + ISO7816_US_PER_MS - 1U) / ISO7816_US_PER_MS;
    HAL_StatusTypeDef hal_stat =
        HAL_SMARTCARD_Receive(&port_ctx->hsc, byte, 1, timeout_ms);
    if (hal_stat == HAL_TIMEOUT) return ISO7816_CHAR_WT;
    if (__HAL_SMARTCARD_GET_FLAG(&port_ctx->hsc, SMARTCARD_FLAG_PERR) ||
        (HAL_SMARTCARD_GetError(&port_ctx->hsc) & SMARTCARD_ERROR_PE)) {
        __HAL_SMARTCARD_CLEAR_PEFLAG(&port_ctx->hsc);
        return ISO7816_CHAR_PARITY;   /* NACK sent automatically (cl. 7.3) */
    }
    return (hal_stat == HAL_OK) ? ISO7816_OK : ISO7816_GEN_HAL;
}

/* Reads the I/O line (IDR readable even in AF mode). */
static bool p_io_read(void *ctx)
{
    stm32u5_ctx_t *port_ctx = ctx;
    return HAL_GPIO_ReadPin(port_ctx->cfg.gpio_tx, port_ctx->cfg.pin_tx) == GPIO_PIN_SET;
}

/* Forces the I/O line (deactivation, cl. 6.4): TX switched to GPIO push-pull. */
static void p_io_drive(void *ctx, bool high)
{
    stm32u5_ctx_t *port_ctx = ctx;
    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Pin = port_ctx->cfg.pin_tx;
    HAL_GPIO_Init(port_ctx->cfg.gpio_tx, &gpio_init);
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_tx, port_ctx->cfg.pin_tx,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
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
        /* busy wait; the 1 MHz timer has a 1 µs resolution */
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
    g_ctx.hal.tx_byte = p_tx_byte;
    g_ctx.hal.rx_byte = p_rx_byte;
    g_ctx.hal.io_read = p_io_read;
    g_ctx.hal.io_drive = p_io_drive;
    g_ctx.hal.micros = p_micros;
    g_ctx.hal.delay_us = p_delay_us;
    return &g_ctx.hal;
}
