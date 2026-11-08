/*
 * pc_card.c — port PC : HAL mock (ligne I/O virtuelle, horloge virtuelle)
 * + carte simulée (ATR, PPS, T=0, T=1) pour tests unitaires/fonctionnels.
 */
#include "pc_card.h"

#include <stdlib.h>
#include <string.h>

#include "iso7816/iso7816_char.h"
#include "iso7816/iso7816_t1.h"

#define EDGES_MAX 160
#define TXQ_MAX   4096
#define RXLOG_MAX 4096

typedef struct {
    uint8_t ins;
    bool    incoming;
    bool    used;
    pc_card_cmd_fn fn;
} cmd_entry_t;

struct pc_card {
    iso7816_hal_t hal;
    iso7816_t *dev;

    /* temps virtuel */
    uint32_t now_us;
    uint32_t clk_hz;

    /* signaux */
    bool vcc_on;
    iso7816_class_t cls;
    bool rst_high;
    bool ifd_tx;
    bool ifd_level;

    /* décodage des caractères émis par l'IFD */
    struct { uint32_t t; bool lvl; } edges[EDGES_MAX];
    int n_edges;
    uint32_t ifd_err_count;

    /* émission carte */
    uint8_t  txq[TXQ_MAX];
    size_t   qhead, qcount;
    bool     cur_active, cur_bad;
    uint32_t cur_start;
    uint8_t  cur_byte;
    uint32_t next_allowed;
    bool     err_active;
    uint32_t err_start;

    /* config carte simulée */
    uint8_t  ts;
    uint8_t  atr[ISO7816_ATR_MAX_BYTES];
    size_t   atr_len;
    bool     silent;
    uint8_t  protocol;
    bool     pps_accept;
    cmd_entry_t cmds[8];
    bool     corrupt_next;

    /* état protocole carte */
    int      phase;                 /* 1 = session active (reset reçu) */
    uint8_t  rxlog[RXLOG_MAX];
    size_t   rxlog_len;
    bool     in_pps;
    uint8_t  pps_buf[8];
    size_t   pps_len, pps_expected;
    uint8_t  t0_hdr[5];
    size_t   t0_hdr_len;
    bool     t0_await_data;
    size_t   t0_data_need, t0_data_len;
    uint8_t  t0_data[256];
    cmd_entry_t *cur_cmd;
    uint8_t  apdu[700];
    size_t   apdu_len;
    /* T=1 */
    int      t1_state;
    uint8_t  t1_nad, t1_pcb, t1_len, t1_lrc;
    uint8_t  t1_inf[ISO7816_T1_INF_MAX];
    uint8_t  t1_inf_len;
    uint8_t  t1_ns;
    /* blocs chaînés en attente (la carte attend l'ack R entre deux I-blocs M=1) */
    uint8_t  pend_blk[8][3 + ISO7816_T1_INF_MAX + 1];
    uint8_t  pend_len[8];
    int      pend_count, pend_head;
};

/* ---------- helpers temps/etu ---------- */

static uint32_t c_etu_us(pc_card_t *c)
{
    if (c->dev) return iso7816_etu_us(c->dev);
    uint32_t f = c->clk_hz ? c->clk_hz : 1000000U;
    return (uint32_t)(372ULL * 1000000ULL / f);
}

static bool c_inverse(pc_card_t *c)
{
    return c->dev ? c->dev->inverse : false;
}

static uint32_t c_cycles_us(pc_card_t *c, uint32_t cycles)
{
    uint32_t f = c->dev ? c->dev->f_hz : (c->clk_hz ? c->clk_hz : 1000000U);
    return (uint32_t)((uint64_t)cycles * 1000000ULL / f);
}

/* ---------- modèle de ligne ---------- */

static void c_pump(pc_card_t *c, uint32_t t)
{
    uint32_t etu = c_etu_us(c);
    if (c->err_active && t >= c->err_start + 2 * etu) c->err_active = false;
    if (c->cur_active && t >= c->cur_start + 10 * etu) {
        c->cur_active = false;
        c->next_allowed = c->cur_start + 12 * etu;
    }
    if (!c->cur_active && c->qcount > 0 && t >= c->next_allowed) {
        c->cur_bad = c->corrupt_next;
        c->corrupt_next = false;
        c->cur_byte = c->txq[c->qhead];
        c->qhead = (c->qhead + 1) % TXQ_MAX;
        c->qcount--;
        c->cur_start = t;
        c->cur_active = true;
        c->next_allowed = t + 12 * etu;
    }
}

static bool c_card_line(pc_card_t *c, uint32_t t)
{
    uint32_t etu = c_etu_us(c);
    if (c->err_active && t >= c->err_start && t < c->err_start + 2 * etu) return false;
    if (!c->cur_active || t < c->cur_start) return true;   /* pull-up : H */
    uint32_t m = (t - c->cur_start) / etu + 1;              /* moment 1..10 */
    if (m > 10) return true;
    bool lvl;
    if (m == 1) {
        lvl = false;
    } else if (m <= 9) {
        int bit = c_inverse(c) ? (9 - (int)m) : ((int)m - 2);
        bool v = ((c->cur_byte >> bit) & 1U) != 0;
        lvl = c_inverse(c) ? !v : v;
    } else {
        bool p = (__builtin_popcount(c->cur_byte) & 1U) != 0;
        if (c->cur_bad) p = !p;
        lvl = c_inverse(c) ? !p : p;
    }
    return lvl;
}

/* ---------- protocole carte ---------- */

static void c_queue(pc_card_t *c, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (c->qcount >= TXQ_MAX) return;
        c->txq[(c->qhead + c->qcount) % TXQ_MAX] = b[i];
        c->qcount++;
    }
}

static void c_t1_queue_block(pc_card_t *c, uint8_t pcb, const uint8_t *inf, uint8_t len)
{
    uint8_t blk[3 + ISO7816_T1_INF_MAX + 1];
    blk[0] = 0x00;
    blk[1] = pcb;
    blk[2] = len;
    if (len) memcpy(blk + 3, inf, len);
    uint8_t lrc = 0;
    for (int i = 0; i < 3 + len; i++) lrc ^= blk[i];
    blk[3 + len] = lrc;
    c_queue(c, blk, (size_t)(4 + len));
}

static void c_t1_pump_pending(pc_card_t *c)
{
    if (c->pend_head < c->pend_count) {
        c_queue(c, c->pend_blk[c->pend_head], c->pend_len[c->pend_head]);
        c->pend_head++;
    }
}

static void c_t1_handle_block(pc_card_t *c)
{
    uint8_t pcb = c->t1_pcb;
    if ((pcb & 0x80) == 0) {                    /* I-block */
        uint8_t ns = (uint8_t)((pcb >> 6) & 1);
        bool more = (pcb & 0x20) != 0;
        if (c->apdu_len + c->t1_inf_len <= sizeof(c->apdu)) {
            memcpy(c->apdu + c->apdu_len, c->t1_inf, c->t1_inf_len);
            c->apdu_len += c->t1_inf_len;
        }
        if (more) {
            /* acquittement R(N(R) = N(S)^1) — règle 2.2 (N(S) reçu, pas le nôtre) */
            c_t1_queue_block(c, (uint8_t)(0x80 | ((ns ^ 1) << 5)), NULL, 0);
        } else {
            cmd_entry_t *e = NULL;
            for (int i = 0; i < 8; i++) {
                if (c->cmds[i].used && c->apdu_len >= 2 && c->cmds[i].ins == c->apdu[1]) e = &c->cmds[i];
            }
            if (e) {
                e->fn(c, c->apdu, c->apdu_len);
            } else {
                uint8_t sw[2] = { 0x6D, 0x00 };
                pc_card_t1_queue_response(c, NULL, 0, 0x6D00);
                (void)sw;
            }
            c->apdu_len = 0;
        }
    } else if ((pcb & 0xC0) == 0xC0) {          /* S-block */
        uint8_t func = pcb & 0x1F;
        if (func == 0x03 && (pcb & 0x20) == 0) { /* S(WTX request) -> S(WTX response) */
            c_t1_queue_block(c, (uint8_t)(0xC0 | 0x20 | 0x03), c->t1_inf, c->t1_inf_len);
        }
        if (func == 0x00 && (pcb & 0x20) == 0) { /* S(RESYNCH request) -> response, état initial */
            c_t1_queue_block(c, (uint8_t)(0xC0 | 0x20 | 0x00), NULL, 0);
            c->t1_ns = 0;
            c->apdu_len = 0;
        }
    } else if ((pcb & 0xC0) == 0x80) {
        /* R-block (ack de l'IFD) : on peut envoyer le bloc chaîné suivant */
        c_t1_pump_pending(c);
    }
}

static void c_t1_byte(pc_card_t *c, uint8_t b)
{
    switch (c->t1_state) {
    case 0: c->t1_nad = b; c->t1_state = 1; break;
    case 1: c->t1_pcb = b; c->t1_state = 2; break;
    case 2:
        c->t1_len = b;
        c->t1_inf_len = 0;
        c->t1_state = (b == 0) ? 4 : 3;
        break;
    case 3:
        if (c->t1_inf_len < ISO7816_T1_INF_MAX) c->t1_inf[c->t1_inf_len++] = b;
        if (c->t1_inf_len >= c->t1_len) c->t1_state = 4;
        break;
    case 4: {
        uint8_t x = (uint8_t)(c->t1_nad ^ c->t1_pcb ^ c->t1_len);
        for (int i = 0; i < c->t1_inf_len; i++) x ^= c->t1_inf[i];
        c->t1_state = 0;
        if ((uint8_t)(x ^ b) != 0) {
            /* LRC faux : R-block avec code erreur 0001 */
            c_t1_queue_block(c, (uint8_t)(0x80 | (c->t1_ns << 5) | 0x01), NULL, 0);
            return;
        }
        c_t1_handle_block(c);
        break;
    }
    default:
        c->t1_state = 0;
        break;
    }
}

static void c_t0_byte(pc_card_t *c, uint8_t b)
{
    if (c->in_pps) {
        if (c->pps_len < sizeof(c->pps_buf)) c->pps_buf[c->pps_len++] = b;
        if (c->pps_len == 2) {
            uint8_t pps0 = c->pps_buf[1];
            c->pps_expected = 2 + ((pps0 & 0x10) ? 1 : 0) + ((pps0 & 0x20) ? 1 : 0) +
                              ((pps0 & 0x40) ? 1 : 0) + 1;
        } else if (c->pps_len >= c->pps_expected) {
            if (c->pps_accept) c_queue(c, c->pps_buf, c->pps_expected);
            c->in_pps = false;
            c->pps_len = 0;
        }
        return;
    }
    if (b == 0xFF && c->t0_hdr_len == 0 && !c->t0_await_data) {
        c->in_pps = true;
        c->pps_len = 0;
        c->pps_buf[c->pps_len++] = b;
        return;
    }
    if (!c->t0_await_data) {
        if (c->t0_hdr_len < 5) c->t0_hdr[c->t0_hdr_len++] = b;
        if (c->t0_hdr_len < 5) return;
        uint8_t ins = c->t0_hdr[1];
        uint8_t p3 = c->t0_hdr[4];
        c->cur_cmd = NULL;
        for (int i = 0; i < 8; i++) {
            if (c->cmds[i].used && c->cmds[i].ins == ins) c->cur_cmd = &c->cmds[i];
        }
        if (!c->cur_cmd) {
            uint8_t sw[2] = { 0x6D, 0x00 };
            c_queue(c, sw, 2);
            c->t0_hdr_len = 0;
            return;
        }
        if (c->cur_cmd->incoming && p3 > 0) {
            c->t0_await_data = true;
            c->t0_data_need = p3;
            c->t0_data_len = 0;
            uint8_t ack = ins;                      /* ACK = INS : toutes les données */
            c_queue(c, &ack, 1);
        } else {
            memcpy(c->apdu, c->t0_hdr, 5);
            c->cur_cmd->fn(c, c->apdu, 5);
            c->t0_hdr_len = 0;
        }
    } else {
        if (c->t0_data_len < sizeof(c->t0_data)) c->t0_data[c->t0_data_len++] = b;
        if (c->t0_data_len >= c->t0_data_need) {
            c->t0_await_data = false;
            memcpy(c->apdu, c->t0_hdr, 5);
            memcpy(c->apdu + 5, c->t0_data, c->t0_data_len);
            if (c->cur_cmd) c->cur_cmd->fn(c, c->apdu, 5 + c->t0_data_len);
            c->t0_hdr_len = 0;
            c->t0_data_len = 0;
        }
    }
}

static void c_on_byte(pc_card_t *c, uint8_t b)
{
    if (c->rxlog_len < RXLOG_MAX) c->rxlog[c->rxlog_len++] = b;
    if (c->phase != 1) return;
    if (c->protocol == 1) c_t1_byte(c, b);
    else c_t0_byte(c, b);
}

/* Décode le caractère que l'IFD vient d'émettre (fronts enregistrés). */
static void c_decode_ifd_char(pc_card_t *c)
{
    if (c->n_edges == 0) return;
    int i0 = -1;
    for (int i = 0; i < c->n_edges; i++) {
        if (!c->edges[i].lvl) { i0 = i; break; }
    }
    if (i0 < 0) return;
    uint32_t t0 = c->edges[i0].t;
    uint32_t etu = c_etu_us(c);

    uint8_t b = 0;
    unsigned ones = 0;
    for (int m = 2; m <= 10; m++) {
        uint32_t s = t0 + (uint32_t)((2 * m - 1) * etu / 2);
        bool lvl = true;
        for (int i = 0; i < c->n_edges; i++) {
            if (c->edges[i].t <= s) lvl = c->edges[i].lvl;
            else break;
        }
        bool v = c_inverse(c) ? !lvl : lvl;
        ones += v ? 1U : 0U;
        if (m <= 9) {
            int bit = c_inverse(c) ? (9 - m) : (m - 2);
            if (v) b |= (uint8_t)(1U << bit);
        }
    }
    /* la carte attend GT après le leading edge du caractère de l'IFD */
    if (t0 + 12 * etu > c->next_allowed) c->next_allowed = t0 + 12 * etu;

    if ((ones & 1U) != 0U) {
        /* parité fausse côté IFD : la carte émet le signal d'erreur à 10,5 etu */
        c->err_active = true;
        c->err_start = t0 + (21 * etu / 2);
        return;
    }
    c_on_byte(c, b);
}

/* ---------- callbacks HAL ---------- */

static int m_init(void *ctx) { (void)ctx; return 0; }
static void m_deinit(void *ctx) { (void)ctx; }

static void m_vcc_on(void *ctx, iso7816_class_t cls)
{
    pc_card_t *c = ctx;
    c->vcc_on = true;
    c->cls = cls;
    c->n_edges = 0;
}

static void m_vcc_off(void *ctx)
{
    pc_card_t *c = ctx;
    c->vcc_on = false;
    c->phase = 0;
}

static void m_rst(void *ctx, bool high)
{
    pc_card_t *c = ctx;
    if (high && !c->rst_high && c->vcc_on) {
        /* cold/warm reset : la carte va répondre à l'ATR */
        c->phase = 1;
        c->n_edges = 0;
        c->rxlog_len = 0;
        c->qcount = 0;
        c->cur_active = false;
        c->err_active = false;
        c->in_pps = false;
        c->t0_hdr_len = 0;
        c->t0_await_data = false;
        c->t1_state = 0;
        c->apdu_len = 0;
        c->t1_ns = 0;
        if (!c->silent) {
            uint8_t hdr[1 + ISO7816_ATR_MAX_BYTES];
            hdr[0] = c->ts;
            memcpy(hdr + 1, c->atr, c->atr_len);
            c_queue(c, hdr, 1 + c->atr_len);
        }
        c->next_allowed = c->now_us + c_cycles_us(c, 400);  /* réponse entre 400 et 40000 cycles */
    }
    c->rst_high = high;
}

static uint32_t m_clk_start(void *ctx, uint32_t hz)
{
    pc_card_t *c = ctx;
    c->clk_hz = hz;
    return hz;
}

static void m_clk_stop(void *ctx, bool keep_high) { (void)ctx; (void)keep_high; }

static void m_io_tx(void *ctx, bool high)
{
    pc_card_t *c = ctx;
    c->ifd_tx = true;
    c->ifd_level = high;
    if (c->n_edges == 0 || c->edges[c->n_edges - 1].lvl != high) {
        if (c->n_edges < EDGES_MAX) {
            c->edges[c->n_edges].t = c->now_us;
            c->edges[c->n_edges].lvl = high;
            c->n_edges++;
        }
    }
}

static void m_io_rx(void *ctx)
{
    pc_card_t *c = ctx;
    c->ifd_tx = false;
    if (c->n_edges >= 1) {
        /* durée entre le premier front à L et la libération de la ligne :
           ~10 etu = caractère, 1..2 etu = signal d'erreur de l'IFD (cl. 7.3) */
        int i0 = -1;
        for (int i = 0; i < c->n_edges; i++) {
            if (!c->edges[i].lvl) { i0 = i; break; }
        }
        uint32_t dur = (i0 >= 0) ? (c->now_us - c->edges[i0].t) : 0;
        if (dur < 3 * c_etu_us(c)) {
            c->ifd_err_count++;
        } else {
            c_decode_ifd_char(c);
        }
    }
    c->n_edges = 0;
}

static bool m_io_read(void *ctx)
{
    pc_card_t *c = ctx;
    c_pump(c, c->now_us);
    if (c->ifd_tx) return c->ifd_level;
    return c_card_line(c, c->now_us);
}

static uint32_t m_micros(void *ctx)
{
    pc_card_t *c = ctx;
    uint32_t t = c->now_us;
    c_pump(c, t);
    c->now_us = t + 1;      /* chaque scrutation fait avancer le temps virtuel de 1 µs */
    return t;
}

static void m_delay_us(void *ctx, uint32_t us)
{
    pc_card_t *c = ctx;
    for (uint32_t i = 0; i < us; i++) {
        c_pump(c, c->now_us);
        c->now_us++;
    }
}

/* ---------- API publique du mock ---------- */

pc_card_t *pc_card_create(void)
{
    pc_card_t *c = calloc(1, sizeof(*c));
    c->hal.ctx = c;
    c->hal.init = m_init;
    c->hal.deinit = m_deinit;
    c->hal.vcc_on = m_vcc_on;
    c->hal.vcc_off = m_vcc_off;
    c->hal.rst_drive = m_rst;
    c->hal.clk_start = m_clk_start;
    c->hal.clk_stop = m_clk_stop;
    c->hal.io_tx = m_io_tx;
    c->hal.io_rx = m_io_rx;
    c->hal.io_read = m_io_read;
    c->hal.micros = m_micros;
    c->hal.delay_us = m_delay_us;
    c->ts = 0x3B;
    c->pps_accept = true;
    return c;
}

void pc_card_destroy(pc_card_t *c) { free(c); }

const iso7816_hal_t *pc_card_hal(pc_card_t *c) { return &c->hal; }

void pc_card_attach(pc_card_t *c, iso7816_t *dev) { c->dev = dev; }

void pc_card_set_atr(pc_card_t *c, uint8_t ts, const uint8_t *atr, size_t len)
{
    c->ts = ts;
    if (len > ISO7816_ATR_MAX_BYTES) len = ISO7816_ATR_MAX_BYTES;
    memcpy(c->atr, atr, len);
    c->atr_len = len;
}

void pc_card_set_protocol(pc_card_t *c, uint8_t t) { c->protocol = t; }
void pc_card_set_pps_accept(pc_card_t *c, bool accept) { c->pps_accept = accept; }
void pc_card_set_silent(pc_card_t *c, bool silent) { c->silent = silent; }
void pc_card_corrupt_next_tx(pc_card_t *c) { c->corrupt_next = true; }

void pc_card_set_cmd_handler(pc_card_t *c, uint8_t ins, bool incoming, pc_card_cmd_fn fn)
{
    for (int i = 0; i < 8; i++) {
        if (!c->cmds[i].used) {
            c->cmds[i].used = true;
            c->cmds[i].ins = ins;
            c->cmds[i].incoming = incoming;
            c->cmds[i].fn = fn;
            return;
        }
    }
}

void pc_card_queue_tx(pc_card_t *c, const uint8_t *b, size_t n) { c_queue(c, b, n); }

void pc_card_queue_sw(pc_card_t *c, uint16_t sw)
{
    uint8_t b[2] = { (uint8_t)(sw >> 8), (uint8_t)(sw & 0xFF) };
    c_queue(c, b, 2);
}

void pc_card_t0_queue_ack_all(pc_card_t *c, uint8_t ins)
{
    c_queue(c, &ins, 1);
}

void pc_card_t0_queue_data_sw(pc_card_t *c, const uint8_t *data, size_t n, uint16_t sw)
{
    c_queue(c, data, n);
    pc_card_queue_sw(c, sw);
}

void pc_card_t1_queue_response(pc_card_t *c, const uint8_t *data, size_t n, uint16_t sw)
{
    uint8_t payload[600];
    if (n > sizeof(payload) - 2) n = sizeof(payload) - 2;
    memcpy(payload, data, n);
    payload[n] = (uint8_t)(sw >> 8);
    payload[n + 1] = (uint8_t)(sw & 0xFF);
    size_t total = n + 2;
    uint8_t ifsc = c->dev ? c->dev->ifsc : 32;
    if (ifsc == 0) ifsc = 32;
    size_t off = 0;
    c->pend_count = 0;
    c->pend_head = 0;
    do {
        size_t chunk = total - off;
        if (chunk > ifsc) chunk = ifsc;
        bool more = (off + chunk) < total;
        uint8_t pcb = (uint8_t)(((c->t1_ns & 1) << 6) | (more ? 0x20 : 0x00));
        uint8_t *blk = c->pend_blk[c->pend_count];
        blk[0] = 0x00;
        blk[1] = pcb;
        blk[2] = (uint8_t)chunk;
        memcpy(blk + 3, payload + off, chunk);
        uint8_t lrc = 0;
        for (size_t i = 0; i < 3 + chunk; i++) lrc ^= blk[i];
        blk[3 + chunk] = lrc;
        c->pend_len[c->pend_count] = (uint8_t)(4 + chunk);
        c->pend_count++;
        c->t1_ns ^= 1;
        off += chunk;
    } while (off < total);
    c_t1_pump_pending(c);   /* le premier bloc part immédiatement (il ack la commande) */
}

size_t pc_card_rxlog(const pc_card_t *c, uint8_t *out, size_t cap)
{
    size_t n = c->rxlog_len < cap ? c->rxlog_len : cap;
    memcpy(out, c->rxlog, n);
    return n;
}

uint32_t pc_card_ifd_err_signals(const pc_card_t *c) { return c->ifd_err_count; }
uint32_t pc_card_now_us(const pc_card_t *c) { return c->now_us; }