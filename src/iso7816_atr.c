#include "iso7816/iso7816_atr.h"

#include <string.h>

#include "iso7816/iso7816_types.h"

/* Nombre total d'octets attendus (T0..TCK) pour une séquence bien formée,
 * ou 0 si la séquence est tronquée/invalide. buf contient T0.. */
static size_t atr_expected_len(const uint8_t *buf, size_t len)
{
    if (len < 1 || len > ISO7816_ATR_MAX_BYTES) {
        return 0;
    }
    uint8_t t0 = buf[0];
    uint8_t yi = (uint8_t)(t0 >> 4);
    uint8_t k = (uint8_t)(t0 & 0x0F);
    size_t idx = 1;
    bool only_t0 = true;
    bool t15 = false;
    bool has_t0 = false;

    while (yi != 0) {
        if (idx >= len) {
            return 0;   /* tronqué : on ne peut pas lire TDi */
        }
        if (yi & 0x1) idx++;    /* TAi */
        if (yi & 0x2) idx++;    /* TBi */
        if (yi & 0x4) idx++;    /* TCi */
        if (yi & 0x8) {         /* TDi */
            if (idx >= len) return 0;
            uint8_t tdi = buf[idx];
            idx++;
            uint8_t t = (uint8_t)(tdi & 0x0F);
            if (t == 15) t15 = true;
            if (t == 0) has_t0 = true;
            if (t != 0) only_t0 = false;
            yi = (uint8_t)(tdi >> 4);
        } else {
            yi = 0;
        }
    }
    /* TCK présent sauf si seul T=0 est indiqué (éventuellement par défaut) — cl. 8.2.5 */
    bool tck = (!only_t0) || (has_t0 && t15);
    if (t15) tck = true;
    if (len > ISO7816_ATR_MAX_BYTES) return 0;
    size_t total = idx + k + (tck ? 1 : 0);
    if (total > ISO7816_ATR_MAX_BYTES) return 0;
    return total;
}

bool iso7816_atr_complete(const uint8_t *buf, size_t len)
{
    size_t expected = atr_expected_len(buf, len);
    return expected != 0 && len >= expected;
}

iso7816_status_t iso7816_atr_parse(iso7816_atr_t *atr, uint8_t ts,
                                    const uint8_t *buf, size_t len)
{
    if (!atr || !buf || len == 0 || len > ISO7816_ATR_MAX_BYTES) {
        return ISO7816_ATR_MALFORMED;
    }
    memset(atr, 0, sizeof(*atr));

    if (ts == 0x3B) {
        atr->inverse = false;
    } else if (ts == 0x3F) {
        atr->inverse = true;
    } else {
        return ISO7816_ATR_TS;
    }
    atr->raw[0] = ts;
    memcpy(atr->raw + 1, buf, len);
    atr->raw_len = len + 1;

    size_t expected = atr_expected_len(buf, len);
    if (expected == 0 || len != expected) {
        return ISO7816_ATR_MALFORMED;
    }

    uint8_t t0 = buf[0];
    atr->t0 = t0;
    atr->k = (uint8_t)(t0 & 0x0F);
    atr->fi = 372;
    atr->di = 1;
    atr->fmax_mhz = 5;
    atr->guard_n = 0;
    atr->wi = 10;
    atr->ifsc = 32;
    atr->cwi = 13;
    atr->bwi = 4;
    atr->use_crc = false;

    uint8_t yi = (uint8_t)(t0 >> 4);
    size_t idx = 1;
    int group = 0;              /* 0 = premier groupe (global), puis type T courant */
    int current_t = -1;
    bool ta15_done = false, tb15_done = false;
    bool ta1_done = false, tc1_done = false, ta2_done = false;
    bool t0_tc_done = false;
    bool t1_ta_done = false, t1_tb_done = false, t1_tc_done = false;
    uint8_t prev_t = 0xFF;

    while (yi != 0) {
        if (idx >= len) return ISO7816_ATR_MALFORMED;
        if (yi & 0x1) {     /* TAi */
            uint8_t ta = buf[idx++];
            if (group == 0 && !ta1_done) {
                ta1_done = true;
                atr->has_ta1 = true;
                uint16_t fi; uint8_t fmax;
                if (iso7816_fi_from_code((uint8_t)(ta >> 4), &fi, &fmax)) {
                    atr->fi = fi;
                    atr->fmax_mhz = fmax;
                }
                uint8_t di;
                if (iso7816_di_from_code((uint8_t)(ta & 0x0F), &di)) {
                    atr->di = di;
                }
            } else if (group == 0 && !ta2_done) {
                ta2_done = true;
                atr->has_ta2 = true;
                atr->ta2 = ta;
            } else if (current_t == 15 && !ta15_done) {
                ta15_done = true;
                atr->has_t15 = true;
                atr->x_clock_stop = (uint8_t)(ta >> 6);
                atr->y_class = (uint8_t)(ta & 0x3F);
            } else if (current_t == 1 && !t1_ta_done) {
                t1_ta_done = true;
                atr->ifsc = ta;
            }
        }
        if (yi & 0x2) {     /* TBi */
            uint8_t tb = buf[idx++];
            if (group == 0) {
                /* TB1/TB2 dépréciés (cl. 8.3) : ignorés */
            } else if (current_t == 15 && !tb15_done) {
                tb15_done = true;
                atr->spu_used = (tb != 0);
                atr->spu_standard = ((tb & 0x80) == 0);
            } else if (current_t == 1 && !t1_tb_done) {
                t1_tb_done = true;
                atr->bwi = (uint8_t)(tb >> 4);
                atr->cwi = (uint8_t)(tb & 0x0F);
            }
        }
        if (yi & 0x4) {     /* TCi */
            uint8_t tc = buf[idx++];
            if (group == 0 && !tc1_done) {
                tc1_done = true;
                atr->guard_n = tc;
            } else if (current_t == 0 && !t0_tc_done) {
                t0_tc_done = true;
                atr->wi = tc;
            } else if (current_t == 1 && !t1_tc_done) {
                t1_tc_done = true;
                atr->use_crc = (tc & 0x1) != 0;
            }
        }
        if (yi & 0x8) {     /* TDi */
            if (idx >= len) return ISO7816_ATR_MALFORMED;
            uint8_t tdi = buf[idx++];
            uint8_t t = (uint8_t)(tdi & 0x0F);
            if (t != 15 && atr->n_protocols < ISO7816_ATR_PROTO_MAX) {
                if (prev_t != 0xFF && t <= prev_t) return ISO7816_ATR_MALFORMED; /* ordre croissant, cl. 8.2.3 */
                atr->protocols[atr->n_protocols++] = t;
                prev_t = t;
            }
            if (t == 15) atr->has_t15 = true;
            if (t == 1) atr->has_t1 = true;
            current_t = t;
            group++;
            yi = (uint8_t)(tdi >> 4);
        } else {
            yi = 0;
        }
    }

    if (atr->k > 0) {
        if (idx + atr->k > len) return ISO7816_ATR_MALFORMED;
        atr->hist_len = atr->k;
        memcpy(atr->historical, buf + idx, atr->k);
        idx += atr->k;
    }

    /* TCK (cl. 8.2.5) : absent seulement si seul T=0 est indiqué (éventuellement par défaut) */
    bool only_t0 = (atr->n_protocols == 0) ||
                   (atr->n_protocols == 1 && atr->protocols[0] == 0);
    bool need_tck = !only_t0 || atr->has_t15;
    if (need_tck) {
        if (idx >= len) return ISO7816_ATR_MALFORMED;
        atr->tck_present = true;
        uint8_t x = 0;
        for (size_t i = 0; i <= idx; i++) {
            x ^= buf[i];
        }
        atr->tck_ok = (x == 0);
        if (!atr->tck_ok) return ISO7816_ATR_TCK;
        idx++;
    }

    if (idx != len) return ISO7816_ATR_MALFORMED;
    return ISO7816_OK;
}