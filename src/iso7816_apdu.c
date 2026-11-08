#include "iso7816/iso7816_apdu.h"

#include <string.h>

/* Décodage selon le tableau 13 (cl. 12.1.3). */
iso7816_status_t iso7816_apdu_decode(const uint8_t *buf, size_t len,
                                     iso7816_apdu_cmd_t *out)
{
    if (!buf || !out || len < ISO7816_APDU_HEADER_LEN) return ISO7816_APDU_INVALID;

    out->cla = buf[0];
    out->ins = buf[1];
    out->p1 = buf[2];
    out->p2 = buf[3];
    out->data = NULL;
    out->nc = 0;
    out->ne = 0;

    if (len == ISO7816_APDU_CASE1_LEN) {
        out->apdu_case = ISO7816_APDU_CASE_1;
        return ISO7816_OK;
    }

    uint8_t lc = buf[ISO7816_APDU_LC_OFFSET_SHORT];
    if (len == ISO7816_APDU_CASE2S_LEN) {
        out->apdu_case = ISO7816_APDU_CASE_2S;
        out->ne = (lc == 0) ? ISO7816_APDU_NE_256 : lc;
        return ISO7816_OK;
    }
    if (lc != ISO7816_APDU_EXTENDED_MARKER) {
        if (len == ISO7816_APDU_DATA_OFFSET_SHORT + lc) {
            out->apdu_case = ISO7816_APDU_CASE_3S;
            out->nc = lc;
            out->data = buf + ISO7816_APDU_DATA_OFFSET_SHORT;
            return ISO7816_OK;
        }
        if (len == ISO7816_APDU_DATA_OFFSET_SHORT + 1 + lc) {
            out->apdu_case = ISO7816_APDU_CASE_4S;
            out->nc = lc;
            out->data = buf + ISO7816_APDU_DATA_OFFSET_SHORT;
            out->ne = (buf[ISO7816_APDU_DATA_OFFSET_SHORT + lc] == 0)
                      ? ISO7816_APDU_NE_256 : buf[ISO7816_APDU_DATA_OFFSET_SHORT + lc];
            return ISO7816_OK;
        }
        return ISO7816_APDU_INVALID;
    }

    if (len < ISO7816_APDU_CASE2E_LEN) return ISO7816_APDU_INVALID;
    uint16_t lc_ext = (uint16_t)((buf[ISO7816_APDU_LC_OFFSET_SHORT + 1] << 8) |
                                 buf[ISO7816_APDU_LC_OFFSET_SHORT + 2]);

    if (len == ISO7816_APDU_CASE2E_LEN) {
        out->apdu_case = ISO7816_APDU_CASE_2E;
        out->ne = (lc_ext == 0) ? ISO7816_APDU_NE_65536 : lc_ext;
        return ISO7816_OK;
    }
    if (lc_ext != 0 && len == ISO7816_APDU_DATA_OFFSET_EXT + lc_ext) {
        out->apdu_case = ISO7816_APDU_CASE_3E;
        out->nc = lc_ext;
        out->data = buf + ISO7816_APDU_DATA_OFFSET_EXT;
        return ISO7816_OK;
    }
    if (lc_ext != 0 && len == ISO7816_APDU_DATA_OFFSET_EXT + 2 + lc_ext) {
        out->apdu_case = ISO7816_APDU_CASE_4E;
        out->nc = lc_ext;
        out->data = buf + ISO7816_APDU_DATA_OFFSET_EXT;
        uint16_t le_ext = (uint16_t)((buf[ISO7816_APDU_DATA_OFFSET_EXT + lc_ext] << 8) |
                                     buf[ISO7816_APDU_DATA_OFFSET_EXT + 1 + lc_ext]);
        out->ne = (le_ext == 0) ? ISO7816_APDU_NE_65536 : le_ext;
        return ISO7816_OK;
    }
    return ISO7816_APDU_INVALID;
}

size_t iso7816_apdu_encode(const iso7816_apdu_cmd_t *cmd, uint8_t *out, size_t cap)
{
    if (!cmd || !out) return 0;
    size_t out_len = 0;
    switch (cmd->apdu_case) {
    case ISO7816_APDU_CASE_1:
        out_len = ISO7816_APDU_CASE1_LEN;
        break;
    case ISO7816_APDU_CASE_2S:
        out_len = ISO7816_APDU_CASE2S_LEN;
        break;
    case ISO7816_APDU_CASE_2E:
        out_len = ISO7816_APDU_CASE2E_LEN;
        break;
    case ISO7816_APDU_CASE_3S:
        if (cmd->nc > ISO7816_APDU_NC_MAX_SHORT) return 0;
        out_len = ISO7816_APDU_DATA_OFFSET_SHORT + cmd->nc;
        break;
    case ISO7816_APDU_CASE_3E:
        if (cmd->nc > ISO7816_APDU_NC_MAX_EXT || cmd->nc == 0) return 0;
        out_len = ISO7816_APDU_DATA_OFFSET_EXT + cmd->nc;
        break;
    case ISO7816_APDU_CASE_4S:
        if (cmd->nc > ISO7816_APDU_NC_MAX_SHORT) return 0;
        out_len = ISO7816_APDU_DATA_OFFSET_SHORT + 1 + cmd->nc;
        break;
    case ISO7816_APDU_CASE_4E:
        if (cmd->nc > ISO7816_APDU_NC_MAX_EXT || cmd->nc == 0) return 0;
        out_len = ISO7816_APDU_DATA_OFFSET_EXT + 2 + cmd->nc;
        break;
    default:
        return 0;
    }
    if (cap < out_len) return 0;

    out[0] = cmd->cla;
    out[1] = cmd->ins;
    out[2] = cmd->p1;
    out[3] = cmd->p2;
    size_t pos = ISO7816_APDU_HEADER_LEN;
    switch (cmd->apdu_case) {
    case ISO7816_APDU_CASE_1:
        break;
    case ISO7816_APDU_CASE_2S:
        out[pos++] = (uint8_t)(cmd->ne >= ISO7816_APDU_NE_256 ? 0 : cmd->ne);
        break;
    case ISO7816_APDU_CASE_2E:
        out[pos++] = ISO7816_APDU_EXTENDED_MARKER;
        out[pos++] = (uint8_t)((cmd->ne & ISO7816_BYTE_HIGH_MASK) >> 8);
        out[pos++] = (uint8_t)(cmd->ne & ISO7816_BYTE_MASK);
        break;
    case ISO7816_APDU_CASE_3S:
        out[pos++] = (uint8_t)cmd->nc;
        memcpy(out + pos, cmd->data, cmd->nc);
        break;
    case ISO7816_APDU_CASE_3E:
        out[pos++] = ISO7816_APDU_EXTENDED_MARKER;
        out[pos++] = (uint8_t)((cmd->nc & ISO7816_BYTE_HIGH_MASK) >> 8);
        out[pos++] = (uint8_t)(cmd->nc & ISO7816_BYTE_MASK);
        memcpy(out + pos, cmd->data, cmd->nc);
        break;
    case ISO7816_APDU_CASE_4S:
        out[pos++] = (uint8_t)cmd->nc;
        memcpy(out + pos, cmd->data, cmd->nc);
        pos += cmd->nc;
        out[pos++] = (uint8_t)(cmd->ne >= ISO7816_APDU_NE_256 ? 0 : cmd->ne);
        break;
    case ISO7816_APDU_CASE_4E:
        out[pos++] = ISO7816_APDU_EXTENDED_MARKER;
        out[pos++] = (uint8_t)((cmd->nc & ISO7816_BYTE_HIGH_MASK) >> 8);
        out[pos++] = (uint8_t)(cmd->nc & ISO7816_BYTE_MASK);
        memcpy(out + pos, cmd->data, cmd->nc);
        pos += cmd->nc;
        out[pos++] = (uint8_t)((cmd->ne & ISO7816_BYTE_HIGH_MASK) >> 8);
        out[pos++] = (uint8_t)(cmd->ne & ISO7816_BYTE_MASK);
        break;
    }
    return out_len;
}
