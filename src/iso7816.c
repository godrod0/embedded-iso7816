#include "iso7816/iso7816.h"

#include <string.h>

#include "iso7816/iso7816_apdu.h"
#include "iso7816/iso7816_char.h"
#include "iso7816/iso7816_pps.h"
#include "iso7816/iso7816_t0.h"
#include "iso7816/iso7816_t1.h"
#include "iso7816_internal.h"

#define WT_ATR_ETU 9600U     /* WT pendant l'ATR et le PPS (cl. 8.1, 9.1) */

static iso7816_status_t rx_char_retry(iso7816_t *d, uint8_t *b, uint32_t timeout_us)
{
    for (uint8_t attempt = 0; attempt <= d->max_retries; attempt++) {
        iso7816_status_t st = iso7816_char_rx(d, b, timeout_us);
        if (st == ISO7816_OK) return ISO7816_OK;
        if (st == ISO7816_CHAR_WT) return st;
        /* PARITY : signal d'erreur émis, on retente la réception */
    }
    return ISO7816_CHAR_PARITY;
}

/* Réception de l'ATR : TS puis T0..TCK (cl. 8). */
static iso7816_status_t rx_atr(iso7816_t *d)
{
    const uint32_t wt = WT_ATR_ETU * iso7816_etu_us(d);

    uint8_t ts;
    iso7816_status_t st = rx_char_retry(d, &ts, wt);
    if (st != ISO7816_OK) return (st == ISO7816_CHAR_WT) ? ISO7816_DEV_NO_ANSWER : st;
    if (ts != 0x3B && ts != 0x3F) return ISO7816_ATR_TS;
    d->inverse = (ts == 0x3F);

    uint8_t buf[ISO7816_ATR_MAX_BYTES];
    size_t len = 0;
    while (len < sizeof(buf)) {
        uint8_t b;
        st = rx_char_retry(d, &b, wt);
        if (st != ISO7816_OK) return (st == ISO7816_CHAR_WT) ? ISO7816_ATR_MALFORMED : st;
        buf[len++] = b;
        if (iso7816_atr_complete(buf, len)) break;
    }
    if (len == 0) return ISO7816_ATR_MALFORMED;

    d->atr_end_us = iso7816_now(d);
    return iso7816_atr_parse(&d->atr, ts, buf, len);
}

void iso7816_apply_protocol(iso7816_t *d)
{
    d->guard_n = d->atr.guard_n;
    d->has_t15 = d->atr.has_t15;
    d->last_dir_rx = false;

    if (d->protocol == 0) {
        /* WT = WI * 960 * Fi/f (cl. 10.2) */
        d->wi = (d->atr.wi == 0) ? 10 : d->atr.wi;
        d->wt_us = (uint32_t)(((uint64_t)d->wi * 960ULL * d->atr.fi * 1000000ULL) / d->f_hz);
        d->use_error_signal = true;
    } else if (d->protocol == 1) {
        d->ifsc = (d->atr.ifsc == 0) ? 32 : d->atr.ifsc;
        d->ifsd = 32;
        d->cwi = d->atr.cwi;
        d->bwi = d->atr.bwi;
        d->use_crc = d->atr.use_crc;
        d->ns_ifd = 0;
        d->ns_card = 0;
        d->use_error_signal = false;    /* pas de signal d'erreur en T=1 (cl. 11.2) */
    }
}

static iso7816_status_t select_protocol(iso7816_t *d)
{
    if (d->atr.has_ta2) {
        d->protocol = (uint8_t)(d->atr.ta2 & 0x0F);   /* mode spécifique (cl. 6.3.1) */
    } else if (d->atr.n_protocols > 0) {
        d->protocol = d->atr.protocols[0];            /* premier protocole offert (cl. 8.2.3) */
    } else {
        d->protocol = 0;                              /* défaut : T=0 */
    }
    if (d->protocol != 0 && d->protocol != 1) {
        return ISO7816_PPS_PROTOCOL;
    }
    iso7816_apply_protocol(d);
    return ISO7816_OK;
}

static void do_deactivate(iso7816_t *d)
{
    d->hal->rst_drive(d->hal->ctx, false);
    d->hal->clk_stop(d->hal->ctx, false);     /* CLK à L (cl. 6.4) */
    d->hal->io_tx(d->hal->ctx, false);        /* I/O à L */
    d->hal->vcc_off(d->hal->ctx);
    d->active = false;
    d->clock_stopped = false;
}

/* Activation + cold reset pour une classe donnée (cl. 6.2.1, 6.2.2). */
static iso7816_status_t try_activate(iso7816_t *d, iso7816_class_t cls)
{
    d->hal->vcc_on(d->hal->ctx, cls);
    d->hal->rst_drive(d->hal->ctx, false);    /* RST à L */
    d->hal->io_rx(d->hal->ctx);               /* I/O en réception côté IFD */

    uint32_t f = d->hal->clk_start(d->hal->ctx, d->clk_reset_hz);
    if (f < 1000000U) f = 1000000U;
    if (f > 5000000U) f = 5000000U;           /* max 5 MHz pendant le reset (cl. 5.2.3) */
    d->f_hz = f;
    d->F = 372;
    d->D = 1;
    d->inverse = false;
    d->guard_n = 0;
    d->has_t15 = false;
    d->use_error_signal = true;
    d->last_dir_rx = false;
    d->wt_us = WT_ATR_ETU * iso7816_etu_us(d);

    /* cold reset : RST maintenu à L au moins 400 cycles (cl. 6.2.2) */
    iso7816_delay_us(d, iso7816_cycles_us(d, 400) + iso7816_cycles_us(d, 10));
    d->hal->rst_drive(d->hal->ctx, true);

    /* la réponse doit commencer entre 400 et 40 000 cycles (cl. 6.2.2) */
    const uint32_t start = iso7816_now(d);
    const uint32_t window = iso7816_cycles_us(d, 40000);
    bool answered = false;
    while ((int32_t)(iso7816_now(d) - start) < (int32_t)window) {
        if (!d->hal->io_read(d->hal->ctx)) {
            answered = true;
            break;
        }
    }
    if (!answered) {
        do_deactivate(d);
        return ISO7816_DEV_NO_ANSWER;
    }

    iso7816_status_t st = rx_atr(d);
    if (st != ISO7816_OK) {
        do_deactivate(d);
        return st;
    }
    d->cls = cls;
    return ISO7816_OK;
}

static bool class_in_mask(iso7816_class_t cls, uint8_t y)
{
    switch (cls) {
    case ISO7816_CLASS_A: return (y & 0x01) != 0;
    case ISO7816_CLASS_B: return (y & 0x02) != 0;
    case ISO7816_CLASS_C: return (y & 0x04) != 0;
    }
    return false;
}

iso7816_status_t iso7816_init(iso7816_t *d, const iso7816_hal_t *hal,
                              const iso7816_config_t *cfg)
{
    if (!d || !hal) return ISO7816_GEN_PARAM;
    memset(d, 0, sizeof(*d));
    d->hal = hal;
    d->clk_reset_hz = (cfg && cfg->clk_hz) ? cfg->clk_hz : 1000000U;
    if (d->clk_reset_hz < 1000000U) d->clk_reset_hz = 1000000U;
    if (d->clk_reset_hz > 5000000U) d->clk_reset_hz = 5000000U;
    d->timeout_ms = (cfg && cfg->timeout_ms) ? cfg->timeout_ms : 1000;
    d->max_retries = (cfg && cfg->max_retries) ? cfg->max_retries : 3;
    if (hal->init) {
        if (hal->init(hal->ctx) != 0) return ISO7816_GEN_HAL;
    }
    return ISO7816_OK;
}

iso7816_status_t iso7816_activate(iso7816_t *d, iso7816_class_t cls)
{
    if (!d || !d->hal) return ISO7816_GEN_PARAM;
    if (d->active) return ISO7816_GEN_STATE;

    /* candidats : la classe demandée d'abord, puis A/B/C (cl. 6.2.4) */
    iso7816_class_t cand[4];
    int n = 0;
    cand[n++] = cls;
    const iso7816_class_t all[3] = { ISO7816_CLASS_A, ISO7816_CLASS_B, ISO7816_CLASS_C };
    for (int i = 0; i < 3; i++) {
        if (all[i] != cls) cand[n++] = all[i];
    }

    iso7816_status_t last = ISO7816_DEV_NO_ANSWER;
    for (int i = 0; i < n; i++) {
        iso7816_status_t st = try_activate(d, cand[i]);
        if (st != ISO7816_OK) {
            if (st == ISO7816_DEV_NO_ANSWER) {
                last = st;
                iso7816_delay_us(d, 10000);   /* délai >= 10 ms entre classes (cl. 6.2.4) */
                continue;
            }
            return st;
        }
        /* si l'ATR indique les classes supportées (Y), vérifier (cl. 6.2.4) */
        if (d->atr.has_t15 && d->atr.y_class != 0 &&
            !class_in_mask(cand[i], d->atr.y_class)) {
            do_deactivate(d);
            iso7816_delay_us(d, 10000);
            last = ISO7816_DEV_CLASS;
            continue;
        }
        st = select_protocol(d);
        if (st != ISO7816_OK) {
            do_deactivate(d);
            return st;
        }
        d->active = true;
        d->cls = cand[i];
        return ISO7816_OK;
    }
    return last;
}

iso7816_status_t iso7816_warm_reset(iso7816_t *d)
{
    if (!d || !d->hal) return ISO7816_GEN_PARAM;
    if (!d->active) return ISO7816_GEN_STATE;

    /* pas avant 4464 cycles après le leading edge de T0 (cl. 6.2.3) */
    uint32_t min_wait = iso7816_cycles_us(d, 4464);
    uint32_t elapsed = iso7816_now(d) - d->atr_end_us;
    if ((int32_t)(min_wait - elapsed) > 0) {
        iso7816_delay_us(d, min_wait - elapsed);
    }

    d->hal->rst_drive(d->hal->ctx, false);    /* RST à L >= 400 cycles, VCC/CLK maintenus */
    iso7816_delay_us(d, iso7816_cycles_us(d, 400) + iso7816_cycles_us(d, 10));
    d->hal->rst_drive(d->hal->ctx, true);

    const uint32_t start = iso7816_now(d);
    const uint32_t window = iso7816_cycles_us(d, 40000);
    bool answered = false;
    while ((int32_t)(iso7816_now(d) - start) < (int32_t)window) {
        if (!d->hal->io_read(d->hal->ctx)) {
            answered = true;
            break;
        }
    }
    if (!answered) {
        do_deactivate(d);
        return ISO7816_DEV_NO_ANSWER;
    }

    iso7816_status_t st = rx_atr(d);
    if (st != ISO7816_OK) {
        do_deactivate(d);
        return st;
    }
    return select_protocol(d);
}

iso7816_status_t iso7816_pps(iso7816_t *d, uint8_t protocol, uint16_t F, uint8_t D)
{
    return iso7816_pps_exchange(d, protocol, F, D);
}

iso7816_status_t iso7816_exchange(iso7816_t *d,
                                  const uint8_t *cmd, size_t cmd_len,
                                  uint8_t *resp, size_t resp_cap,
                                  size_t *resp_len, uint16_t *sw)
{
    if (!d || !d->hal) return ISO7816_GEN_PARAM;
    if (!d->active) return ISO7816_GEN_STATE;
    if (!cmd || !resp || !resp_len || !sw) return ISO7816_GEN_PARAM;

    if (d->protocol == 0) {
        return iso7816_t0_exchange(d, cmd, cmd_len, resp, resp_cap, resp_len, sw);
    }
    if (d->protocol == 1) {
        return iso7816_t1_exchange(d, cmd, cmd_len, resp, resp_cap, resp_len, sw);
    }
    return ISO7816_GEN_STATE;
}

iso7816_status_t iso7816_clock_stop(iso7816_t *d)
{
    if (!d || !d->hal || !d->active) return ISO7816_GEN_STATE;
    if (!d->atr.has_t15 || d->atr.x_clock_stop == 0 || d->clock_stopped) {
        return ISO7816_GEN_STATE;   /* clock stop non supporté (tableau 9) */
    }
    /* I/O resté à H pendant au moins 1860 cycles (cl. 6.3.2) */
    const uint32_t dur = iso7816_cycles_us(d, 1860);
    const uint32_t timeout = iso7816_cycles_us(d, 40000);
    const uint32_t start = iso7816_now(d);
    uint32_t last_l = start;
    for (;;) {
        if (!d->hal->io_read(d->hal->ctx)) {
            last_l = iso7816_now(d);
        } else if ((int32_t)(iso7816_now(d) - last_l) >= (int32_t)dur) {
            break;
        }
        if ((int32_t)(iso7816_now(d) - start) >= (int32_t)timeout) {
            return ISO7816_GEN_STATE;
        }
    }
    d->hal->clk_stop(d->hal->ctx, d->atr.x_clock_stop == 2);
    d->clock_stopped = true;
    return ISO7816_OK;
}

iso7816_status_t iso7816_clock_restart(iso7816_t *d)
{
    if (!d || !d->hal || !d->clock_stopped) return ISO7816_GEN_STATE;
    d->hal->clk_start(d->hal->ctx, d->f_hz);
    iso7816_delay_us(d, iso7816_cycles_us(d, 700));   /* reprise >= 700 cycles (cl. 6.3.2) */
    d->clock_stopped = false;
    return ISO7816_OK;
}

iso7816_status_t iso7816_deactivate(iso7816_t *d)
{
    if (!d || !d->hal) return ISO7816_GEN_PARAM;
    do_deactivate(d);
    return ISO7816_OK;
}

const iso7816_atr_t *iso7816_get_atr(const iso7816_t *d)
{
    return d ? &d->atr : NULL;
}

uint8_t iso7816_get_protocol(const iso7816_t *d)
{
    return d ? d->protocol : 0xFF;
}