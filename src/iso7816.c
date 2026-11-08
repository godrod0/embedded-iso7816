#include "iso7816/iso7816.h"

#include <string.h>

#include "iso7816/iso7816_apdu.h"
#include "iso7816/iso7816_char.h"
#include "iso7816/iso7816_pps.h"
#include "iso7816/iso7816_t0.h"
#include "iso7816/iso7816_t1.h"
#include "iso7816_internal.h"

static iso7816_status_t rx_char_retry(iso7816_t *dev, uint8_t *byte, uint32_t timeout_us)
{
    for (uint8_t attempt = 0; attempt <= dev->max_retries; attempt++) {
        iso7816_status_t stat = iso7816_char_rx(dev, byte, timeout_us);
        if (stat == ISO7816_OK) return ISO7816_OK;
        if (stat == ISO7816_CHAR_WT) return stat;
        /* PARITY : signal d'erreur émis, on retente la réception */
    }
    return ISO7816_CHAR_PARITY;
}

/* Réception de l'ATR : TS puis T0..TCK (cl. 8). */
static iso7816_status_t rx_atr(iso7816_t *dev)
{
    const uint32_t wt_us = ISO7816_WT_ATR_PPS_ETU * iso7816_etu_us(dev);

    uint8_t ts;
    iso7816_status_t stat = rx_char_retry(dev, &ts, wt_us);
    if (stat != ISO7816_OK) return (stat == ISO7816_CHAR_WT) ? ISO7816_DEV_NO_ANSWER : stat;
    if (ts != ISO7816_ATR_TS_DIRECT && ts != ISO7816_ATR_TS_INVERSE) return ISO7816_ATR_TS;
    dev->inverse = (ts == ISO7816_ATR_TS_INVERSE);

    uint8_t buf[ISO7816_ATR_MAX_BYTES];
    size_t len = 0;
    while (len < sizeof(buf)) {
        uint8_t byte;
        stat = rx_char_retry(dev, &byte, wt_us);
        if (stat != ISO7816_OK) return (stat == ISO7816_CHAR_WT) ? ISO7816_ATR_MALFORMED : stat;
        buf[len++] = byte;
        if (iso7816_atr_complete(buf, len)) break;
    }
    if (len == 0) return ISO7816_ATR_MALFORMED;

    dev->atr_end_us = iso7816_now(dev);
    return iso7816_atr_parse(&dev->atr, ts, buf, len);
}

void iso7816_apply_protocol(iso7816_t *dev)
{
    dev->guard_n = dev->atr.guard_n;
    dev->has_t15 = dev->atr.has_t15;
    dev->last_dir_rx = false;

    if (dev->protocol == ISO7816_PROTOCOL_T0) {
        /* WT = WI * 960 * Fi/f (cl. 10.2) */
        dev->wi = (dev->atr.wi == 0) ? ISO7816_WI_DEFAULT : dev->atr.wi;
        dev->wt_us = (uint32_t)(((uint64_t)dev->wi * ISO7816_T0_WT_ETU_PER_WI *
                                  dev->atr.fi * ISO7816_US_PER_SECOND) / dev->f_hz);
        dev->use_error_signal = true;
    } else if (dev->protocol == ISO7816_PROTOCOL_T1) {
        dev->ifsc = (dev->atr.ifsc == 0) ? ISO7816_IFSC_DEFAULT : dev->atr.ifsc;
        dev->ifsd = ISO7816_IFSD_DEFAULT;
        dev->cwi = dev->atr.cwi;
        dev->bwi = dev->atr.bwi;
        dev->use_crc = dev->atr.use_crc;
        dev->ns_ifd = 0;
        dev->ns_card = 0;
        dev->use_error_signal = false;  /* pas de signal d'erreur en T=1 (cl. 11.2) */
    }
}

static iso7816_status_t select_protocol(iso7816_t *dev)
{
    if (dev->atr.has_ta2) {
        dev->protocol = (uint8_t)(dev->atr.ta2 & ISO7816_TA2_PROTO_MASK); /* mode spécifique (cl. 6.3.1) */
    } else if (dev->atr.n_protocols > 0) {
        dev->protocol = dev->atr.protocols[0];          /* premier protocole offert (cl. 8.2.3) */
    } else {
        dev->protocol = ISO7816_PROTOCOL_T0;           /* défaut : T=0 */
    }
    if (dev->protocol != ISO7816_PROTOCOL_T0 && dev->protocol != ISO7816_PROTOCOL_T1) {
        return ISO7816_PPS_PROTOCOL;
    }
    iso7816_apply_protocol(dev);
    return ISO7816_OK;
}

static void do_deactivate(iso7816_t *dev)
{
    dev->hal->rst_drive(dev->hal->ctx, false);
    dev->hal->clk_stop(dev->hal->ctx, false);     /* CLK à L (cl. 6.4) */
    dev->hal->io_tx(dev->hal->ctx, false);        /* I/O à L */
    dev->hal->vcc_off(dev->hal->ctx);
    dev->active = false;
    dev->clock_stopped = false;
}

/* Activation + cold reset pour une classe donnée (cl. 6.2.1, 6.2.2). */
static iso7816_status_t try_activate(iso7816_t *dev, iso7816_class_t cls)
{
    dev->hal->vcc_on(dev->hal->ctx, cls);
    dev->hal->rst_drive(dev->hal->ctx, false);    /* RST à L */
    dev->hal->io_rx(dev->hal->ctx);               /* I/O en réception côté IFD */

    uint32_t clk_hz = dev->hal->clk_start(dev->hal->ctx, dev->clk_reset_hz);
    if (clk_hz < ISO7816_CLK_RESET_MIN_HZ) clk_hz = ISO7816_CLK_RESET_MIN_HZ;
    if (clk_hz > ISO7816_CLK_RESET_MAX_HZ) clk_hz = ISO7816_CLK_RESET_MAX_HZ; /* max 5 MHz pendant le reset (cl. 5.2.3) */
    dev->f_hz = clk_hz;
    dev->F = ISO7816_F_DEFAULT;
    dev->D = ISO7816_D_DEFAULT;
    dev->inverse = false;
    dev->guard_n = ISO7816_GUARD_N_DEFAULT;
    dev->has_t15 = false;
    dev->use_error_signal = true;
    dev->last_dir_rx = false;
    dev->wt_us = ISO7816_WT_ATR_PPS_ETU * iso7816_etu_us(dev);

    /* cold reset : RST maintenu à L au moins 400 cycles (cl. 6.2.2) */
    iso7816_delay_us(dev, iso7816_cycles_us(dev, ISO7816_COLD_RESET_RST_LOW_CYCLES) +
                          iso7816_cycles_us(dev, ISO7816_RESET_MARGIN_CYCLES));
    dev->hal->rst_drive(dev->hal->ctx, true);

    /* la réponse doit commencer entre 400 et 40 000 cycles (cl. 6.2.2) */
    const uint32_t start = iso7816_now(dev);
    const uint32_t window = iso7816_cycles_us(dev, ISO7816_RESET_ANSWER_MAX_CYCLES);
    bool answered = false;
    while ((int32_t)(iso7816_now(dev) - start) < (int32_t)window) {
        if (!dev->hal->io_read(dev->hal->ctx)) {
            answered = true;
            break;
        }
    }
    if (!answered) {
        do_deactivate(dev);
        return ISO7816_DEV_NO_ANSWER;
    }

    iso7816_status_t stat = rx_atr(dev);
    if (stat != ISO7816_OK) {
        do_deactivate(dev);
        return stat;
    }
    dev->cls = cls;
    return ISO7816_OK;
}

static bool class_in_mask(iso7816_class_t cls, uint8_t y_class_mask)
{
    switch (cls) {
    case ISO7816_CLASS_A: return (y_class_mask & ISO7816_T15_CLASS_A_MASK) != 0;
    case ISO7816_CLASS_B: return (y_class_mask & ISO7816_T15_CLASS_B_MASK) != 0;
    case ISO7816_CLASS_C: return (y_class_mask & ISO7816_T15_CLASS_C_MASK) != 0;
    }
    return false;
}

iso7816_status_t iso7816_init(iso7816_t *dev, const iso7816_hal_t *hal,
                              const iso7816_config_t *cfg)
{
    if (!dev || !hal) return ISO7816_GEN_PARAM;
    memset(dev, 0, sizeof(*dev));
    dev->hal = hal;
    dev->clk_reset_hz = (cfg && cfg->clk_hz) ? cfg->clk_hz : ISO7816_CLK_RESET_MIN_HZ;
    if (dev->clk_reset_hz < ISO7816_CLK_RESET_MIN_HZ) dev->clk_reset_hz = ISO7816_CLK_RESET_MIN_HZ;
    if (dev->clk_reset_hz > ISO7816_CLK_RESET_MAX_HZ) dev->clk_reset_hz = ISO7816_CLK_RESET_MAX_HZ;
    dev->timeout_ms = (cfg && cfg->timeout_ms) ? cfg->timeout_ms : ISO7816_DEFAULT_TIMEOUT_MS;
    dev->max_retries = (cfg && cfg->max_retries) ? cfg->max_retries : ISO7816_DEFAULT_MAX_RETRIES;
    if (hal->init) {
        if (hal->init(hal->ctx) != 0) return ISO7816_GEN_HAL;
    }
    return ISO7816_OK;
}

iso7816_status_t iso7816_activate(iso7816_t *dev, iso7816_class_t cls)
{
    if (!dev || !dev->hal) return ISO7816_GEN_PARAM;
    if (dev->active) return ISO7816_GEN_STATE;

    /* candidats : la classe demandée d'abord, puis A/B/C (cl. 6.2.4) */
    iso7816_class_t class_cand[ISO7816_CLASS_COUNT + 1];
    int n_cand = 0;
    class_cand[n_cand++] = cls;
    const iso7816_class_t all_classes[ISO7816_CLASS_COUNT] =
        { ISO7816_CLASS_A, ISO7816_CLASS_B, ISO7816_CLASS_C };
    for (int i = 0; i < (int)ISO7816_CLASS_COUNT; i++) {
        if (all_classes[i] != cls) class_cand[n_cand++] = all_classes[i];
    }

    iso7816_status_t last_stat = ISO7816_DEV_NO_ANSWER;
    for (int i = 0; i < n_cand; i++) {
        iso7816_status_t stat = try_activate(dev, class_cand[i]);
        if (stat != ISO7816_OK) {
            if (stat == ISO7816_DEV_NO_ANSWER) {
                last_stat = stat;
                iso7816_delay_us(dev, ISO7816_CLASS_SWITCH_DELAY_US); /* >= 10 ms entre classes (cl. 6.2.4) */
                continue;
            }
            return stat;
        }
        /* si l'ATR indique les classes supportées (Y), vérifier (cl. 6.2.4) */
        if (dev->atr.has_t15 && dev->atr.y_class != 0 &&
            !class_in_mask(class_cand[i], dev->atr.y_class)) {
            do_deactivate(dev);
            iso7816_delay_us(dev, ISO7816_CLASS_SWITCH_DELAY_US);
            last_stat = ISO7816_DEV_CLASS;
            continue;
        }
        stat = select_protocol(dev);
        if (stat != ISO7816_OK) {
            do_deactivate(dev);
            return stat;
        }
        dev->active = true;
        dev->cls = class_cand[i];
        return ISO7816_OK;
    }
    return last_stat;
}

iso7816_status_t iso7816_warm_reset(iso7816_t *dev)
{
    if (!dev || !dev->hal) return ISO7816_GEN_PARAM;
    if (!dev->active) return ISO7816_GEN_STATE;

    /* pas avant 4464 cycles après le leading edge de T0 (cl. 6.2.3) */
    uint32_t min_wait = iso7816_cycles_us(dev, ISO7816_WARM_RESET_MIN_CYCLES);
    uint32_t elapsed = iso7816_now(dev) - dev->atr_end_us;
    if ((int32_t)(min_wait - elapsed) > 0) {
        iso7816_delay_us(dev, min_wait - elapsed);
    }

    dev->hal->rst_drive(dev->hal->ctx, false);    /* RST à L >= 400 cycles, VCC/CLK maintenus */
    iso7816_delay_us(dev, iso7816_cycles_us(dev, ISO7816_COLD_RESET_RST_LOW_CYCLES) +
                          iso7816_cycles_us(dev, ISO7816_RESET_MARGIN_CYCLES));
    dev->hal->rst_drive(dev->hal->ctx, true);

    const uint32_t start = iso7816_now(dev);
    const uint32_t window = iso7816_cycles_us(dev, ISO7816_RESET_ANSWER_MAX_CYCLES);
    bool answered = false;
    while ((int32_t)(iso7816_now(dev) - start) < (int32_t)window) {
        if (!dev->hal->io_read(dev->hal->ctx)) {
            answered = true;
            break;
        }
    }
    if (!answered) {
        do_deactivate(dev);
        return ISO7816_DEV_NO_ANSWER;
    }

    iso7816_status_t stat = rx_atr(dev);
    if (stat != ISO7816_OK) {
        do_deactivate(dev);
        return stat;
    }
    return select_protocol(dev);
}

iso7816_status_t iso7816_pps(iso7816_t *dev, uint8_t protocol, uint16_t F, uint8_t D)
{
    return iso7816_pps_exchange(dev, protocol, F, D);
}

iso7816_status_t iso7816_exchange(iso7816_t *dev,
                                  const uint8_t *cmd, size_t cmd_len,
                                  uint8_t *resp, size_t resp_cap,
                                  size_t *resp_len, uint16_t *sw)
{
    if (!dev || !dev->hal) return ISO7816_GEN_PARAM;
    if (!dev->active) return ISO7816_GEN_STATE;
    if (!cmd || !resp || !resp_len || !sw) return ISO7816_GEN_PARAM;

    if (dev->protocol == ISO7816_PROTOCOL_T0) {
        return iso7816_t0_exchange(dev, cmd, cmd_len, resp, resp_cap, resp_len, sw);
    }
    if (dev->protocol == ISO7816_PROTOCOL_T1) {
        return iso7816_t1_exchange(dev, cmd, cmd_len, resp, resp_cap, resp_len, sw);
    }
    return ISO7816_GEN_STATE;
}

iso7816_status_t iso7816_clock_stop(iso7816_t *dev)
{
    if (!dev || !dev->hal || !dev->active) return ISO7816_GEN_STATE;
    if (!dev->atr.has_t15 || dev->atr.x_clock_stop == ISO7816_TA15_X_NONE || dev->clock_stopped) {
        return ISO7816_GEN_STATE;   /* clock stop non supporté (tableau 9) */
    }
    /* I/O resté à H pendant au moins 1860 cycles (cl. 6.3.2) */
    const uint32_t dur = iso7816_cycles_us(dev, ISO7816_CLOCK_STOP_IO_HIGH_CYCLES);
    const uint32_t timeout = iso7816_cycles_us(dev, ISO7816_RESET_ANSWER_MAX_CYCLES);
    const uint32_t start = iso7816_now(dev);
    uint32_t last_low_us = start;
    for (;;) {
        if (!dev->hal->io_read(dev->hal->ctx)) {
            last_low_us = iso7816_now(dev);
        } else if ((int32_t)(iso7816_now(dev) - last_low_us) >= (int32_t)dur) {
            break;
        }
        if ((int32_t)(iso7816_now(dev) - start) >= (int32_t)timeout) {
            return ISO7816_GEN_STATE;
        }
    }
    dev->hal->clk_stop(dev->hal->ctx, dev->atr.x_clock_stop == ISO7816_TA15_X_HIGH);
    dev->clock_stopped = true;
    return ISO7816_OK;
}

iso7816_status_t iso7816_clock_restart(iso7816_t *dev)
{
    if (!dev || !dev->hal || !dev->clock_stopped) return ISO7816_GEN_STATE;
    dev->hal->clk_start(dev->hal->ctx, dev->f_hz);
    iso7816_delay_us(dev, iso7816_cycles_us(dev, ISO7816_CLOCK_RESTART_MIN_CYCLES)); /* reprise >= 700 cycles (cl. 6.3.2) */
    dev->clock_stopped = false;
    return ISO7816_OK;
}

iso7816_status_t iso7816_deactivate(iso7816_t *dev)
{
    if (!dev || !dev->hal) return ISO7816_GEN_PARAM;
    do_deactivate(dev);
    return ISO7816_OK;
}

const iso7816_atr_t *iso7816_get_atr(const iso7816_t *dev)
{
    return dev ? &dev->atr : NULL;
}

uint8_t iso7816_get_protocol(const iso7816_t *dev)
{
    return dev ? dev->protocol : ISO7816_PROTOCOL_NONE;
}
