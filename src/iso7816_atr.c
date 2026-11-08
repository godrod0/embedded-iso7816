#include "iso7816/iso7816_atr.h"

#include <string.h>

#include "iso7816/iso7816_types.h"

/* Total number of expected bytes (T0..TCK) for a well-formed sequence,
 * or 0 if the sequence is truncated/invalid. buf holds T0.. */
static size_t atr_expected_len(const uint8_t *buf, size_t len)
{
    if (len < 1 || len > ISO7816_ATR_MAX_BYTES) {
        return 0;
    }
    uint8_t t0 = buf[0];
    uint8_t yi = (uint8_t)(t0 >> ISO7816_ATR_YI_SHIFT);
    uint8_t hist_count = (uint8_t)(t0 & ISO7816_ATR_T0_K_MASK);
    size_t offset = 1;
    bool only_t0 = true;
    bool t15 = false;
    bool has_t0 = false;

    while (yi != 0) {
        if (offset >= len) {
            return 0;   /* truncated: cannot read TDi */
        }
        if (yi & ISO7816_ATR_YI_TA_BIT) offset++;
        if (yi & ISO7816_ATR_YI_TB_BIT) offset++;
        if (yi & ISO7816_ATR_YI_TC_BIT) offset++;
        if (yi & ISO7816_ATR_YI_TD_BIT) {
            if (offset >= len) return 0;
            uint8_t tdi = buf[offset];
            offset++;
            uint8_t proto = (uint8_t)(tdi & ISO7816_ATR_TD_PROTO_MASK);
            if (proto == ISO7816_ATR_T15) t15 = true;
            if (proto == 0) has_t0 = true;
            if (proto != 0) only_t0 = false;
            yi = (uint8_t)(tdi >> ISO7816_ATR_YI_SHIFT);
        } else {
            yi = 0;
        }
    }
    /* TCK present unless only T=0 is indicated (possibly by default) — cl. 8.2.5 */
    bool tck = (!only_t0) || (has_t0 && t15);
    if (t15) tck = true;
    size_t total = offset + hist_count + (tck ? 1 : 0);
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

    if (ts == ISO7816_ATR_TS_DIRECT) {
        atr->inverse = false;
    } else if (ts == ISO7816_ATR_TS_INVERSE) {
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
    atr->k = (uint8_t)(t0 & ISO7816_ATR_T0_K_MASK);
    atr->fi = ISO7816_F_DEFAULT;
    atr->di = ISO7816_D_DEFAULT;
    atr->fmax_mhz = ISO7816_FMAX_DEFAULT_MHZ;
    atr->guard_n = ISO7816_GUARD_N_DEFAULT;
    atr->wi = ISO7816_WI_DEFAULT;
    atr->ifsc = ISO7816_IFSC_DEFAULT;
    atr->cwi = ISO7816_CWI_DEFAULT;
    atr->bwi = ISO7816_BWI_DEFAULT;
    atr->use_crc = false;

    uint8_t yi = (uint8_t)(t0 >> ISO7816_ATR_YI_SHIFT);
    size_t offset = 1;
    int group = 0;              /* 0 = first group (global), then current T type */
    int current_t = -1;
    bool ta15_done = false, tb15_done = false;
    bool ta1_done = false, tc1_done = false, ta2_done = false;
    bool t0_tc_done = false;
    bool t1_ta_done = false, t1_tb_done = false, t1_tc_done = false;
    uint8_t prev_t = ISO7816_PROTOCOL_NONE;

    while (yi != 0) {
        if (offset >= len) return ISO7816_ATR_MALFORMED;
        if (yi & ISO7816_ATR_YI_TA_BIT) {
            uint8_t ta = buf[offset++];
            if (group == 0 && !ta1_done) {
                ta1_done = true;
                atr->has_ta1 = true;
                uint16_t fi; uint8_t fmax;
                if (iso7816_fi_from_code((uint8_t)(ta >> ISO7816_TA1_FI_SHIFT), &fi, &fmax)) {
                    atr->fi = fi;
                    atr->fmax_mhz = fmax;
                }
                uint8_t di;
                if (iso7816_di_from_code((uint8_t)(ta & ISO7816_TA1_DI_MASK), &di)) {
                    atr->di = di;
                }
            } else if (group == 0 && !ta2_done) {
                ta2_done = true;
                atr->has_ta2 = true;
                atr->ta2 = ta;
            } else if (current_t == ISO7816_ATR_T15 && !ta15_done) {
                ta15_done = true;
                atr->has_t15 = true;
                atr->x_clock_stop = (uint8_t)(ta >> ISO7816_TA15_X_SHIFT);
                atr->y_class = (uint8_t)(ta & ISO7816_TA15_Y_MASK);
            } else if (current_t == 1 && !t1_ta_done) {
                t1_ta_done = true;
                atr->ifsc = ta;
            }
        }
        if (yi & ISO7816_ATR_YI_TB_BIT) {
            uint8_t tb = buf[offset++];
            if (group == 0) {
                /* TB1/TB2 deprecated (cl. 8.3): ignored */
            } else if (current_t == ISO7816_ATR_T15 && !tb15_done) {
                tb15_done = true;
                atr->spu_used = (tb != 0);
                atr->spu_standard = ((tb & ISO7816_TB15_SPU_STANDARD_MASK) == 0);
            } else if (current_t == 1 && !t1_tb_done) {
                t1_tb_done = true;
                atr->bwi = (uint8_t)(tb >> ISO7816_TB1_BWI_SHIFT);
                atr->cwi = (uint8_t)(tb & ISO7816_TB1_CWI_MASK);
            }
        }
        if (yi & ISO7816_ATR_YI_TC_BIT) {
            uint8_t tc = buf[offset++];
            if (group == 0 && !tc1_done) {
                tc1_done = true;
                atr->guard_n = tc;
            } else if (current_t == 0 && !t0_tc_done) {
                t0_tc_done = true;
                atr->wi = tc;
            } else if (current_t == 1 && !t1_tc_done) {
                t1_tc_done = true;
                atr->use_crc = (tc & ISO7816_TC1_CRC_BIT) != 0;
            }
        }
        if (yi & ISO7816_ATR_YI_TD_BIT) {
            if (offset >= len) return ISO7816_ATR_MALFORMED;
            uint8_t tdi = buf[offset++];
            uint8_t proto = (uint8_t)(tdi & ISO7816_ATR_TD_PROTO_MASK);
            if (proto != ISO7816_ATR_T15 && atr->n_protocols < ISO7816_ATR_PROTO_MAX) {
                if (prev_t != ISO7816_PROTOCOL_NONE && proto <= prev_t) {
                    return ISO7816_ATR_MALFORMED;   /* increasing order, cl. 8.2.3 */
                }
                atr->protocols[atr->n_protocols++] = proto;
                prev_t = proto;
            }
            if (proto == ISO7816_ATR_T15) atr->has_t15 = true;
            if (proto == 1) atr->has_t1 = true;
            current_t = proto;
            group++;
            yi = (uint8_t)(tdi >> ISO7816_ATR_YI_SHIFT);
        } else {
            yi = 0;
        }
    }

    if (atr->k > 0) {
        if (offset + atr->k > len) return ISO7816_ATR_MALFORMED;
        atr->hist_len = atr->k;
        memcpy(atr->historical, buf + offset, atr->k);
        offset += atr->k;
    }

    /* TCK (cl. 8.2.5): absent only if only T=0 is indicated (possibly by default) */
    bool only_t0 = (atr->n_protocols == 0) ||
                   (atr->n_protocols == 1 && atr->protocols[0] == 0);
    bool need_tck = !only_t0 || atr->has_t15;
    if (need_tck) {
        if (offset >= len) return ISO7816_ATR_MALFORMED;
        atr->tck_present = true;
        uint8_t xor_acc = 0;
        for (size_t i = 0; i <= offset; i++) {
            xor_acc ^= buf[i];
        }
        atr->tck_ok = (xor_acc == 0);
        if (!atr->tck_ok) return ISO7816_ATR_TCK;
        offset++;
    }

    if (offset != len) return ISO7816_ATR_MALFORMED;
    return ISO7816_OK;
}
