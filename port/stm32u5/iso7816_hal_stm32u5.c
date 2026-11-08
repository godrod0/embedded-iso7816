/*
 * iso7816_hal_stm32u5.c — implémentation STM32U5 de iso7816_hal_t,
 * basée sur la bibliothèque SMARTCARD de la HAL STM32 (stm32u5xx_hal_smartcard.h).
 *
 * Le USART en mode smartcard gère :
 *   - C7 I/O sur TX (open-drain) : trames complètes via HAL_SMARTCARD_Transmit/Receive,
 *     parité paire + NACK automatique (cl. 7.3) et guard time (USART_GTPR) en matériel ;
 *   - C3 CLK sur CK : fréquence = f_USART / (2 * Prescaler).
 *
 * Non compilé par défaut (option CMake ISO7816_BUILD_STM32U5_PORT=ON,
 * à activer dans un projet STM32Cube avec le HAL STM32U5 disponible,
 * incluant stm32u5xx_hal_smartcard.c).
 */
#include "iso7816_hal_stm32u5.h"

#include <string.h>

typedef struct {
    iso7816_hal_stm32u5_config_t cfg;
    iso7816_hal_t hal;
    SMARTCARD_HandleTypeDef hsc;
    uint32_t usart_clk_hz;   /* horloge de l'USART (APB) */
    uint32_t clk_hz;         /* fréquence CK courante */
} stm32u5_ctx_t;

static stm32u5_ctx_t g_ctx;

/* ---------- alimentation / reset (GPIO, inchangé) ---------- */

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

/* Rétablit TX en AF open-drain (mode smartcard) si la broche est passée en GPIO. */
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

/* Callback HAL appelé par HAL_SMARTCARD_Init : horloge USART + GPIO AF TX/CK. */
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

    /* RST à L, VCC coupé */
    GPIO_InitTypeDef gpio_init = { 0 };
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Pin = port_ctx->cfg.pin_rst;
    HAL_GPIO_Init(port_ctx->cfg.gpio_rst, &gpio_init);
    HAL_GPIO_WritePin(port_ctx->cfg.gpio_rst, port_ctx->cfg.pin_rst, GPIO_PIN_RESET);
    vcc_select(port_ctx, ISO7816_CLASS_A);
    p_vcc_off(ctx);

    /* USART en mode smartcard : trame 9 bits (8 données + parité paire), 1,5 stop */
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
       USART1/USART6 sur APB2, les autres sur APB1 (à ajuster selon le modèle) */
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

/* C3 CLK : CK = f_USART / (2 * Prescaler). Renvoie la fréquence réelle. */
static uint32_t p_clk_start(void *ctx, uint32_t hz)
{
    stm32u5_ctx_t *port_ctx = ctx;
    if (!port_ctx->cfg.usart || hz == 0) return 0;

    /* CK repasse en AF (après un éventuel clock stop en GPIO) */
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

/* Arrêt d'horloge (cl. 6.3.2) : CK basculée en GPIO et forcée au niveau demandé. */
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

/* Émission d'une trame complète (start + 8 bits + parité) par le périphérique.
   Renvoie ISO7816_CHAR_PARITY si la carte a émis le signal d'erreur (NACK)
   pendant le guard time (cl. 7.3). */
static iso7816_status_t p_tx_byte(void *ctx, uint8_t byte)
{
    stm32u5_ctx_t *port_ctx = ctx;
    tx_ensure_af(port_ctx);
    HAL_StatusTypeDef hal_stat =
        HAL_SMARTCARD_Transmit(&port_ctx->hsc, &byte, 1, ISO7816_DEFAULT_TIMEOUT_MS);
    if (__HAL_SMARTCARD_GET_FLAG(&port_ctx->hsc, SMARTCARD_FLAG_PERR) ||
        (HAL_SMARTCARD_GetError(&port_ctx->hsc) & SMARTCARD_ERROR_PE)) {
        __HAL_SMARTCARD_CLEAR_PEFLAG(&port_ctx->hsc);
        return ISO7816_CHAR_PARITY;   /* NACK reçu : répéter le caractère (cl. 7.3) */
    }
    return (hal_stat == HAL_OK) ? ISO7816_OK : ISO7816_GEN_HAL;
}

/* Réception d'une trame complète. timeout_us = WT ;
   parité fausse -> le périphérique émet le NACK automatiquement et renvoie
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
        return ISO7816_CHAR_PARITY;   /* NACK émis automatiquement (cl. 7.3) */
    }
    return (hal_stat == HAL_OK) ? ISO7816_OK : ISO7816_GEN_HAL;
}

/* Lecture de la ligne I/O (IDR lisible même en AF). */
static bool p_io_read(void *ctx)
{
    stm32u5_ctx_t *port_ctx = ctx;
    return HAL_GPIO_ReadPin(port_ctx->cfg.gpio_tx, port_ctx->cfg.pin_tx) == GPIO_PIN_SET;
}

/* Force la ligne I/O (désactivation, cl. 6.4) : TX passée en GPIO push-pull. */
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
    g_ctx.hal.tx_byte = p_tx_byte;
    g_ctx.hal.rx_byte = p_rx_byte;
    g_ctx.hal.io_read = p_io_read;
    g_ctx.hal.io_drive = p_io_drive;
    g_ctx.hal.micros = p_micros;
    g_ctx.hal.delay_us = p_delay_us;
    return &g_ctx.hal;
}
