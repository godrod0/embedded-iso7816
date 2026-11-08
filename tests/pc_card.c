/*
 * pc_card.c — port PC : HAL mock (ligne I/O virtuelle, horloge virtuelle)
 * + carte simulée (ATR, PPS, T=0, T=1) pour tests unitaires/fonctionnels.
 */
#include "pc_card.h"

#include <stdlib.h>
#include <string.h>

#include "iso7816/iso7816_char.h"
#include "iso7816/iso7816_t1.h"

#define EDGES_MAX           160
#define TXQ_MAX             4096
#define RXLOG_MAX           4096
#define CMD_SLOTS           8    /* handlers de commandes enregistrables */
#define APDU_BUF_MAX        700
#define T0_DATA_MAX         256
#define MOCK_ERR_SIGNAL_ETU 2U   /* durée du signal d'erreur émis par la carte (1..2 etu, cl. 7.3) */

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
    struct { uint32_t time_us; bool level; } edges[EDGES_MAX];
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
    cmd_entry_t cmds[CMD_SLOTS];
    bool     corrupt_next;

    /* état protocole carte */
    int      phase;                 /* 1 = session active (reset reçu) */
    uint8_t  rxlog[RXLOG_MAX];
    size_t   rxlog_len;
    bool     in_pps;
    uint8_t  pps_buf[ISO7816_PPS_RESP_MAX];
    size_t   pps_len, pps_expected;
    uint8_t  t0_hdr[ISO7816_T0_HEADER_LEN];
    size_t   t0_hdr_len;
    bool     t0_await_data;
    size_t   t0_data_need, t0_data_len;
    uint8_t  t0_data[T0_DATA_MAX];
    cmd_entry_t *cur_cmd;
    uint8_t  apdu[APDU_BUF_MAX];
    size_t   apdu_len;
    /* T=1 */
    int      t1_state;
    uint8_t  t1_nad, t1_pcb, t1_len, t1_lrc;
    uint8_t  t1_inf[ISO7816_T1_INF_MAX];
    uint8_t  t1_inf_len;
    uint8_t  t1_ns;
    /* blocs chaînés en attente (la carte attend l'ack R entre deux I-blocs M=1) */
    uint8_t  pend_blk[CMD_SLOTS][ISO7816_T1_HEADER_LEN + ISO7816_T1_INF_MAX + 1];
    uint8_t  pend_len[CMD_SLOTS];
    int      pend_count, pend_head;
};

/* ---------- helpers temps/etu ---------- */

static uint32_t card_etu_us(pc_card_t *card)
{
    if (card->dev) return iso7816_etu_us(card->dev);
    uint32_t clk_hz = card->clk_hz ? card->clk_hz : ISO7816_PORT_TIME_BASE_HZ;
    return (uint32_t)((uint64_t)ISO7816_F_DEFAULT * ISO7816_US_PER_SECOND / clk_hz);
}

static bool card_inverse(pc_card_t *card)
{
    return card->dev ? card->dev->inverse : false;
}

static uint32_t card_cycles_us(pc_card_t *card, uint32_t cycles)
{
    uint32_t clk_hz = card->dev ? card->dev->f_hz : (card->clk_hz ? card->clk_hz : ISO7816_PORT_TIME_BASE_HZ);
    return (uint32_t)((uint64_t)cycles * ISO7816_US_PER_SECOND / clk_hz);
}

/* ---------- modèle de ligne ---------- */

static void card_pump(pc_card_t *card, uint32_t time_us)
{
    uint32_t etu = card_etu_us(card);
    if (card->err_active && time_us >= card->err_start + MOCK_ERR_SIGNAL_ETU * etu) card->err_active = false;
    if (card->cur_active && time_us >= card->cur_start + ISO7816_CHAR_MOMENT_TOTAL * etu) {
        card->cur_active = false;
        card->next_allowed = card->cur_start + ISO7816_CHAR_GT_ETU * etu;
    }
    if (!card->cur_active && card->qcount > 0 && time_us >= card->next_allowed) {
        card->cur_bad = card->corrupt_next;
        card->corrupt_next = false;
        card->cur_byte = card->txq[card->qhead];
        card->qhead = (card->qhead + 1) % TXQ_MAX;
        card->qcount--;
        card->cur_start = time_us;
        card->cur_active = true;
        card->next_allowed = time_us + ISO7816_CHAR_GT_ETU * etu;
    }
}

static bool card_line(pc_card_t *card, uint32_t time_us)
{
    uint32_t etu = card_etu_us(card);
    if (card->err_active && time_us >= card->err_start &&
        time_us < card->err_start + MOCK_ERR_SIGNAL_ETU * etu) return false;
    if (!card->cur_active || time_us < card->cur_start) return true;   /* pull-up : H */
    uint32_t moment = (time_us - card->cur_start) / etu + 1;            /* moment 1..10 */
    if (moment > ISO7816_CHAR_MOMENT_TOTAL) return true;
    bool level;
    if (moment == ISO7816_CHAR_MOMENT_START) {
        level = false;
    } else if (moment <= ISO7816_CHAR_MOMENT_LAST_DATA) {
        int bit = card_inverse(card) ? ((int)ISO7816_CHAR_MOMENT_LAST_DATA - (int)moment)
                                     : ((int)moment - (int)ISO7816_CHAR_MOMENT_FIRST_DATA);
        bool value = ((card->cur_byte >> bit) & 1U) != 0;
        level = card_inverse(card) ? !value : value;
    } else {
        bool parity = (((unsigned)__builtin_popcount(card->cur_byte) & 1U) != 0U);
        if (card->cur_bad) parity = !parity;
        level = card_inverse(card) ? !parity : parity;
    }
    return level;
}

/* ---------- protocole carte ---------- */

static void card_queue(pc_card_t *card, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (card->qcount >= TXQ_MAX) return;
        card->txq[(card->qhead + card->qcount) % TXQ_MAX] = data[i];
        card->qcount++;
    }
}

static void card_t1_queue_block(pc_card_t *card, uint8_t pcb, const uint8_t *inf, uint8_t len)
{
    uint8_t blk[ISO7816_T1_HEADER_LEN + ISO7816_T1_INF_MAX + 1];
    blk[0] = ISO7816_T1_NAD_DEFAULT;
    blk[1] = pcb;
    blk[2] = len;
    if (len) memcpy(blk + ISO7816_T1_HEADER_LEN, inf, len);
    uint8_t lrc = 0;
    for (size_t i = 0; i < ISO7816_T1_HEADER_LEN + len; i++) lrc ^= blk[i];
    blk[ISO7816_T1_HEADER_LEN + len] = lrc;
    card_queue(card, blk, (size_t)(ISO7816_T1_HEADER_LEN + 1 + len));
}

static void card_t1_pump_pending(pc_card_t *card)
{
    if (card->pend_head < card->pend_count) {
        card_queue(card, card->pend_blk[card->pend_head], card->pend_len[card->pend_head]);
        card->pend_head++;
    }
}

static void card_t1_handle_block(pc_card_t *card)
{
    uint8_t pcb = card->t1_pcb;
    if ((pcb & ISO7816_T1_PCB_IBLOCK_BIT) == 0) {   /* I-block */
        bool ns = (pcb & ISO7816_T1_PCB_NS_BIT) != 0;
        bool more = (pcb & ISO7816_T1_PCB_MORE_BIT) != 0;
        if (card->apdu_len + card->t1_inf_len <= sizeof(card->apdu)) {
            memcpy(card->apdu + card->apdu_len, card->t1_inf, card->t1_inf_len);
            card->apdu_len += card->t1_inf_len;
        }
        if (more) {
            /* acquittement R(N(R) = N(S)^1) — règle 2.2 (N(S) reçu, pas le nôtre) */
            card_t1_queue_block(card,
                                (uint8_t)(ISO7816_T1_PCB_RBLOCK |
                                          ((ns ^ 1U) != 0 ? ISO7816_T1_PCB_NR_BIT : 0)),
                                NULL, 0);
        } else {
            cmd_entry_t *entry = NULL;
            for (int i = 0; i < CMD_SLOTS; i++) {
                if (card->cmds[i].used && card->apdu_len >= 2 && card->cmds[i].ins == card->apdu[1]) {
                    entry = &card->cmds[i];
                }
            }
            if (entry) {
                entry->fn(card, card->apdu, card->apdu_len);
            } else {
                pc_card_t1_queue_response(card, NULL, 0, 0x6D00);   /* INS non supporté */
            }
            card->apdu_len = 0;
        }
    } else if ((pcb & ISO7816_T1_PCB_TYPE_MASK) == ISO7816_T1_PCB_SBLOCK) {
        uint8_t s_func = pcb & ISO7816_T1_PCB_FUNC_MASK;
        if (s_func == ISO7816_T1_S_WTX && (pcb & ISO7816_T1_PCB_RESP_BIT) == 0) {
            /* S(WTX request) -> S(WTX response) */
            card_t1_queue_block(card,
                                (uint8_t)(ISO7816_T1_PCB_SBLOCK | ISO7816_T1_PCB_RESP_BIT | ISO7816_T1_S_WTX),
                                card->t1_inf, card->t1_inf_len);
        }
        if (s_func == ISO7816_T1_S_RESYNCH && (pcb & ISO7816_T1_PCB_RESP_BIT) == 0) {
            /* S(RESYNCH request) -> response, état initial */
            card_t1_queue_block(card,
                                (uint8_t)(ISO7816_T1_PCB_SBLOCK | ISO7816_T1_PCB_RESP_BIT | ISO7816_T1_S_RESYNCH),
                                NULL, 0);
            card->t1_ns = 0;
            card->apdu_len = 0;
        }
    } else if ((pcb & ISO7816_T1_PCB_TYPE_MASK) == ISO7816_T1_PCB_RBLOCK) {
        /* R-block (ack de l'IFD) : on peut envoyer le bloc chaîné suivant */
        card_t1_pump_pending(card);
    }
}

static void card_t1_byte(pc_card_t *card, uint8_t byte)
{
    switch (card->t1_state) {
    case 0: card->t1_nad = byte; card->t1_state = 1; break;
    case 1: card->t1_pcb = byte; card->t1_state = 2; break;
    case 2:
        card->t1_len = byte;
        card->t1_inf_len = 0;
        card->t1_state = (byte == 0) ? 4 : 3;
        break;
    case 3:
        if (card->t1_inf_len < ISO7816_T1_INF_MAX) card->t1_inf[card->t1_inf_len++] = byte;
        if (card->t1_inf_len >= card->t1_len) card->t1_state = 4;
        break;
    case 4: {
        uint8_t xor_acc = (uint8_t)(card->t1_nad ^ card->t1_pcb ^ card->t1_len);
        for (int i = 0; i < (int)card->t1_inf_len; i++) xor_acc ^= card->t1_inf[i];
        card->t1_state = 0;
        if ((uint8_t)(xor_acc ^ byte) != 0) {
            /* LRC faux : R-block avec code erreur EDC */
            card_t1_queue_block(card,
                                (uint8_t)(ISO7816_T1_PCB_RBLOCK |
                                          ((card->t1_ns & 1U) != 0 ? ISO7816_T1_PCB_NR_BIT : 0) |
                                          ISO7816_T1_R_ERR_EDC),
                                NULL, 0);
            return;
        }
        card_t1_handle_block(card);
        break;
    }
    default:
        card->t1_state = 0;
        break;
    }
}

static void card_t0_byte(pc_card_t *card, uint8_t byte)
{
    if (card->in_pps) {
        if (card->pps_len < sizeof(card->pps_buf)) card->pps_buf[card->pps_len++] = byte;
        if (card->pps_len == 2) {
            uint8_t pps0 = card->pps_buf[1];
            card->pps_expected = 2 + ((pps0 & ISO7816_PPS0_PPS1_BIT) ? 1 : 0) +
                                 ((pps0 & ISO7816_PPS0_PPS2_BIT) ? 1 : 0) +
                                 ((pps0 & ISO7816_PPS0_PPS3_BIT) ? 1 : 0) + 1;
        } else if (card->pps_len >= card->pps_expected) {
            if (card->pps_accept) card_queue(card, card->pps_buf, card->pps_expected);
            card->in_pps = false;
            card->pps_len = 0;
        }
        return;
    }
    if (byte == ISO7816_PPS_PPSS && card->t0_hdr_len == 0 && !card->t0_await_data) {
        card->in_pps = true;
        card->pps_len = 0;
        card->pps_buf[card->pps_len++] = byte;
        return;
    }
    if (!card->t0_await_data) {
        if (card->t0_hdr_len < ISO7816_T0_HEADER_LEN) card->t0_hdr[card->t0_hdr_len++] = byte;
        if (card->t0_hdr_len < ISO7816_T0_HEADER_LEN) return;
        uint8_t ins = card->t0_hdr[1];
        uint8_t p3 = card->t0_hdr[4];
        card->cur_cmd = NULL;
        for (int i = 0; i < CMD_SLOTS; i++) {
            if (card->cmds[i].used && card->cmds[i].ins == ins) card->cur_cmd = &card->cmds[i];
        }
        if (!card->cur_cmd) {
            uint8_t sw[ISO7816_SW_LEN] = { 0x6D, 0x00 };
            card_queue(card, sw, ISO7816_SW_LEN);
            card->t0_hdr_len = 0;
            return;
        }
        if (card->cur_cmd->incoming && p3 > 0) {
            card->t0_await_data = true;
            card->t0_data_need = p3;
            card->t0_data_len = 0;
            uint8_t ack = ins;                      /* ACK = INS : toutes les données */
            card_queue(card, &ack, 1);
        } else {
            memcpy(card->apdu, card->t0_hdr, ISO7816_T0_HEADER_LEN);
            card->cur_cmd->fn(card, card->apdu, ISO7816_T0_HEADER_LEN);
            card->t0_hdr_len = 0;
        }
    } else {
        if (card->t0_data_len < sizeof(card->t0_data)) card->t0_data[card->t0_data_len++] = byte;
        if (card->t0_data_len >= card->t0_data_need) {
            card->t0_await_data = false;
            memcpy(card->apdu, card->t0_hdr, ISO7816_T0_HEADER_LEN);
            memcpy(card->apdu + ISO7816_T0_HEADER_LEN, card->t0_data, card->t0_data_len);
            if (card->cur_cmd) card->cur_cmd->fn(card, card->apdu, ISO7816_T0_HEADER_LEN + card->t0_data_len);
            card->t0_hdr_len = 0;
            card->t0_data_len = 0;
        }
    }
}

static void card_on_byte(pc_card_t *card, uint8_t byte)
{
    if (card->rxlog_len < RXLOG_MAX) card->rxlog[card->rxlog_len++] = byte;
    if (card->phase != 1) return;
    if (card->protocol == ISO7816_PROTOCOL_T1) card_t1_byte(card, byte);
    else card_t0_byte(card, byte);
}

/* Décode le caractère que l'IFD vient d'émettre (fronts enregistrés). */
static void card_decode_ifd_char(pc_card_t *card)
{
    if (card->n_edges == 0) return;
    int i0 = -1;
    for (int i = 0; i < card->n_edges; i++) {
        if (!card->edges[i].level) { i0 = i; break; }
    }
    if (i0 < 0) return;
    uint32_t t_start = card->edges[i0].time_us;
    uint32_t etu = card_etu_us(card);

    uint8_t byte = 0;
    unsigned ones = 0;
    for (int moment = (int)ISO7816_CHAR_MOMENT_FIRST_DATA;
         moment <= (int)ISO7816_CHAR_MOMENT_TOTAL; moment++) {
        uint32_t sample_us = t_start + (uint32_t)((2 * moment - 1) * etu / ISO7816_CHAR_HALF_ETU_DIV);
        bool level = true;
        for (int i = 0; i < card->n_edges; i++) {
            if (card->edges[i].time_us <= sample_us) level = card->edges[i].level;
            else break;
        }
        bool value = card_inverse(card) ? !level : level;
        ones += value ? 1U : 0U;
        if (moment <= (int)ISO7816_CHAR_MOMENT_LAST_DATA) {
            int bit = card_inverse(card) ? ((int)ISO7816_CHAR_MOMENT_LAST_DATA - moment)
                                         : (moment - (int)ISO7816_CHAR_MOMENT_FIRST_DATA);
            if (value) byte |= (uint8_t)(1U << bit);
        }
    }
    /* la carte attend le GT après le leading edge du caractère de l'IFD */
    if (t_start + ISO7816_CHAR_GT_ETU * etu > card->next_allowed) {
        card->next_allowed = t_start + ISO7816_CHAR_GT_ETU * etu;
    }

    if ((ones & 1U) != 0U) {
        /* parité fausse côté IFD : la carte émet le signal d'erreur à 10,5 etu */
        card->err_active = true;
        card->err_start = t_start + (ISO7816_CHAR_ERR_SIGNAL_HALF_ETU * etu / ISO7816_CHAR_HALF_ETU_DIV);
        return;
    }
    card_on_byte(card, byte);
}

/* ---------- callbacks HAL ---------- */

static int m_init(void *ctx) { (void)ctx; return 0; }
static void m_deinit(void *ctx) { (void)ctx; }

static void m_vcc_on(void *ctx, iso7816_class_t cls)
{
    pc_card_t *card = ctx;
    card->vcc_on = true;
    card->cls = cls;
    card->n_edges = 0;
}

static void m_vcc_off(void *ctx)
{
    pc_card_t *card = ctx;
    card->vcc_on = false;
    card->phase = 0;
}

static void m_rst(void *ctx, bool high)
{
    pc_card_t *card = ctx;
    if (high && !card->rst_high && card->vcc_on) {
        /* cold/warm reset : la carte va répondre à l'ATR */
        card->phase = 1;
        card->n_edges = 0;
        card->rxlog_len = 0;
        card->qcount = 0;
        card->cur_active = false;
        card->err_active = false;
        card->in_pps = false;
        card->t0_hdr_len = 0;
        card->t0_await_data = false;
        card->t1_state = 0;
        card->apdu_len = 0;
        card->t1_ns = 0;
        if (!card->silent) {
            uint8_t hdr[1 + ISO7816_ATR_MAX_BYTES];
            hdr[0] = card->ts;
            memcpy(hdr + 1, card->atr, card->atr_len);
            card_queue(card, hdr, 1 + card->atr_len);
        }
        /* réponse entre 400 et 40 000 cycles (cl. 6.2.2) */
        card->next_allowed = card->now_us + card_cycles_us(card, ISO7816_COLD_RESET_RST_LOW_CYCLES);
    }
    card->rst_high = high;
}

static uint32_t m_clk_start(void *ctx, uint32_t hz)
{
    pc_card_t *card = ctx;
    card->clk_hz = hz;
    return hz;
}

static void m_clk_stop(void *ctx, bool keep_high) { (void)ctx; (void)keep_high; }

static void m_io_tx(void *ctx, bool high)
{
    pc_card_t *card = ctx;
    card->ifd_tx = true;
    card->ifd_level = high;
    if (card->n_edges == 0 || card->edges[card->n_edges - 1].level != high) {
        if (card->n_edges < EDGES_MAX) {
            card->edges[card->n_edges].time_us = card->now_us;
            card->edges[card->n_edges].level = high;
            card->n_edges++;
        }
    }
}

static void m_io_rx(void *ctx)
{
    pc_card_t *card = ctx;
    card->ifd_tx = false;
    if (card->n_edges >= 1) {
        /* durée entre le premier front à L et la libération de la ligne :
           ~10 etu = caractère, 1..2 etu = signal d'erreur de l'IFD (cl. 7.3) */
        int i0 = -1;
        for (int i = 0; i < card->n_edges; i++) {
            if (!card->edges[i].level) { i0 = i; break; }
        }
        uint32_t dur = (i0 >= 0) ? (card->now_us - card->edges[i0].time_us) : 0;
        if (dur < 3 * card_etu_us(card)) {
            card->ifd_err_count++;
        } else {
            card_decode_ifd_char(card);
        }
    }
    card->n_edges = 0;
}

static bool m_io_read(void *ctx)
{
    pc_card_t *card = ctx;
    card_pump(card, card->now_us);
    if (card->ifd_tx) return card->ifd_level;
    return card_line(card, card->now_us);
}

static uint32_t m_micros(void *ctx)
{
    pc_card_t *card = ctx;
    uint32_t now_us = card->now_us;
    card_pump(card, now_us);
    card->now_us = now_us + 1;  /* chaque scrutation fait avancer le temps virtuel de 1 µs */
    return now_us;
}

static void m_delay_us(void *ctx, uint32_t us)
{
    pc_card_t *card = ctx;
    for (uint32_t i = 0; i < us; i++) {
        card_pump(card, card->now_us);
        card->now_us++;
    }
}

/* ---------- API publique du mock ---------- */

pc_card_t *pc_card_create(void)
{
    pc_card_t *card = calloc(1, sizeof(*card));
    card->hal.ctx = card;
    card->hal.init = m_init;
    card->hal.deinit = m_deinit;
    card->hal.vcc_on = m_vcc_on;
    card->hal.vcc_off = m_vcc_off;
    card->hal.rst_drive = m_rst;
    card->hal.clk_start = m_clk_start;
    card->hal.clk_stop = m_clk_stop;
    card->hal.io_tx = m_io_tx;
    card->hal.io_rx = m_io_rx;
    card->hal.io_read = m_io_read;
    card->hal.micros = m_micros;
    card->hal.delay_us = m_delay_us;
    card->ts = ISO7816_ATR_TS_DIRECT;
    card->pps_accept = true;
    return card;
}

void pc_card_destroy(pc_card_t *card) { free(card); }

const iso7816_hal_t *pc_card_hal(pc_card_t *card) { return &card->hal; }

void pc_card_attach(pc_card_t *card, iso7816_t *dev) { card->dev = dev; }

void pc_card_set_atr(pc_card_t *card, uint8_t ts, const uint8_t *atr, size_t len)
{
    card->ts = ts;
    if (len > ISO7816_ATR_MAX_BYTES) len = ISO7816_ATR_MAX_BYTES;
    memcpy(card->atr, atr, len);
    card->atr_len = len;
}

void pc_card_set_protocol(pc_card_t *card, uint8_t protocol) { card->protocol = protocol; }
void pc_card_set_pps_accept(pc_card_t *card, bool accept) { card->pps_accept = accept; }
void pc_card_set_silent(pc_card_t *card, bool silent) { card->silent = silent; }
void pc_card_corrupt_next_tx(pc_card_t *card) { card->corrupt_next = true; }

void pc_card_set_cmd_handler(pc_card_t *card, uint8_t ins, bool incoming, pc_card_cmd_fn fn)
{
    for (int i = 0; i < CMD_SLOTS; i++) {
        if (!card->cmds[i].used) {
            card->cmds[i].used = true;
            card->cmds[i].ins = ins;
            card->cmds[i].incoming = incoming;
            card->cmds[i].fn = fn;
            return;
        }
    }
}

void pc_card_queue_tx(pc_card_t *card, const uint8_t *data, size_t len) { card_queue(card, data, len); }

void pc_card_queue_sw(pc_card_t *card, uint16_t sw)
{
    uint8_t sw_bytes[ISO7816_SW_LEN] = { (uint8_t)(sw >> 8), (uint8_t)(sw & ISO7816_BYTE_MASK) };
    card_queue(card, sw_bytes, ISO7816_SW_LEN);
}

void pc_card_t0_queue_ack_all(pc_card_t *card, uint8_t ins)
{
    card_queue(card, &ins, 1);
}

void pc_card_t0_queue_data_sw(pc_card_t *card, const uint8_t *data, size_t len, uint16_t sw)
{
    card_queue(card, data, len);
    pc_card_queue_sw(card, sw);
}

void pc_card_t1_queue_response(pc_card_t *card, const uint8_t *data, size_t len, uint16_t sw)
{
    uint8_t payload[600];
    if (len > sizeof(payload) - ISO7816_SW_LEN) len = sizeof(payload) - ISO7816_SW_LEN;
    memcpy(payload, data, len);
    payload[len] = (uint8_t)(sw >> 8);
    payload[len + 1] = (uint8_t)(sw & ISO7816_BYTE_MASK);
    size_t total = len + ISO7816_SW_LEN;
    uint8_t ifsc = card->dev ? card->dev->ifsc : ISO7816_IFSC_DEFAULT;
    if (ifsc == 0) ifsc = ISO7816_IFSC_DEFAULT;
    size_t off = 0;
    card->pend_count = 0;
    card->pend_head = 0;
    do {
        size_t chunk = total - off;
        if (chunk > ifsc) chunk = ifsc;
        bool more = (off + chunk) < total;
        uint8_t pcb = (uint8_t)(((card->t1_ns & 1U) != 0 ? ISO7816_T1_PCB_NS_BIT : 0) |
                                (more ? ISO7816_T1_PCB_MORE_BIT : 0));
        uint8_t *blk = card->pend_blk[card->pend_count];
        blk[0] = ISO7816_T1_NAD_DEFAULT;
        blk[1] = pcb;
        blk[2] = (uint8_t)chunk;
        memcpy(blk + ISO7816_T1_HEADER_LEN, payload + off, chunk);
        uint8_t lrc = 0;
        for (size_t i = 0; i < ISO7816_T1_HEADER_LEN + chunk; i++) lrc ^= blk[i];
        blk[ISO7816_T1_HEADER_LEN + chunk] = lrc;
        card->pend_len[card->pend_count] = (uint8_t)(ISO7816_T1_HEADER_LEN + 1 + chunk);
        card->pend_count++;
        card->t1_ns ^= 1;
        off += chunk;
    } while (off < total);
    card_t1_pump_pending(card);   /* le premier bloc part immédiatement (il ack la commande) */
}

size_t pc_card_rxlog(const pc_card_t *card, uint8_t *out, size_t cap)
{
    size_t len = card->rxlog_len < cap ? card->rxlog_len : cap;
    memcpy(out, card->rxlog, len);
    return len;
}

uint32_t pc_card_ifd_err_signals(const pc_card_t *card) { return card->ifd_err_count; }
uint32_t pc_card_now_us(const pc_card_t *card) { return card->now_us; }
