#include "iso7816/iso7816_apdu.h"

#include <string.h>

/* Décodage selon le tableau 13 (cl. 12.1.3). */
iso7816_status_t iso7816_apdu_decode(const uint8_t *buf, size_t len,
                                     iso7816_apdu_cmd_t *out)
{
    if (!buf || !out || len < 4) return ISO7816_APDU_INVALID;

    out->cla = buf[0];
    out->ins = buf[1];
    out->p1 = buf[2];
    out->p2 = buf[3];
    out->data = NULL;
    out->nc = 0;
    out->ne = 0;

    if (len == 4) {
        out->apdu_case = ISO7816_APDU_CASE_1;
        return ISO7816_OK;
    }

    uint8_t c5 = buf[4];
    if (len == 5) {
        out->apdu_case = ISO7816_APDU_CASE_2S;
        out->ne = (c5 == 0) ? 256 : c5;
        return ISO7816_OK;
    }
    if (c5 != 0x00) {
        if (len == 5 + c5) {
            out->apdu_case = ISO7816_APDU_CASE_3S;
            out->nc = c5;
            out->data = buf + 5;
            return ISO7816_OK;
        }
        if (len == 6 + c5) {
            out->apdu_case = ISO7816_APDU_CASE_4S;
            out->nc = c5;
            out->data = buf + 5;
            out->ne = (buf[5 + c5] == 0) ? 256 : buf[5 + c5];
            return ISO7816_OK;
        }
        return ISO7816_APDU_INVALID;
    }

    if (len < 7) return ISO7816_APDU_INVALID;
    uint16_t c67 = (uint16_t)((buf[5] << 8) | buf[6]);

    if (len == 7) {
        out->apdu_case = ISO7816_APDU_CASE_2E;
        out->ne = (c67 == 0) ? 65536 : c67;
        return ISO7816_OK;
    }
    if (c67 != 0 && len == 7 + c67) {
        out->apdu_case = ISO7816_APDU_CASE_3E;
        out->nc = c67;
        out->data = buf + 7;
        return ISO7816_OK;
    }
    if (c67 != 0 && len == 9 + c67) {
        out->apdu_case = ISO7816_APDU_CASE_4E;
        out->nc = c67;
        out->data = buf + 7;
        uint16_t le = (uint16_t)((buf[7 + c67] << 8) | buf[8 + c67]);
        out->ne = (le == 0) ? 65536 : le;
        return ISO7816_OK;
    }
    return ISO7816_APDU_INVALID;
}

size_t iso7816_apdu_encode(const iso7816_apdu_cmd_t *c, uint8_t *out, size_t cap)
{
    if (!c || !out) return 0;
    size_t n = 0;
    switch (c->apdu_case) {
    case ISO7816_APDU_CASE_1:
        n = 4;
        break;
    case ISO7816_APDU_CASE_2S:
        n = 5;
        break;
    case ISO7816_APDU_CASE_2E:
        n = 7;
        break;
    case ISO7816_APDU_CASE_3S:
        if (c->nc > 255) return 0;
        n = 5 + c->nc;
        break;
    case ISO7816_APDU_CASE_3E:
        if (c->nc > 65535 || c->nc == 0) return 0;
        n = 7 + c->nc;
        break;
    case ISO7816_APDU_CASE_4S:
        if (c->nc > 255) return 0;
        n = 6 + c->nc;
        break;
    case ISO7816_APDU_CASE_4E:
        if (c->nc > 65535 || c->nc == 0) return 0;
        n = 9 + c->nc;
        break;
    default:
        return 0;
    }
    if (cap < n) return 0;

    out[0] = c->cla;
    out[1] = c->ins;
    out[2] = c->p1;
    out[3] = c->p2;
    size_t p = 4;
    switch (c->apdu_case) {
    case ISO7816_APDU_CASE_1:
        break;
    case ISO7816_APDU_CASE_2S:
        out[p++] = (uint8_t)(c->ne >= 256 ? 0 : c->ne);
        break;
    case ISO7816_APDU_CASE_2E:
        out[p++] = 0x00;
        out[p++] = (uint8_t)((c->ne & 0xFF00) >> 8);
        out[p++] = (uint8_t)(c->ne & 0xFF);
        break;
    case ISO7816_APDU_CASE_3S:
        out[p++] = (uint8_t)c->nc;
        memcpy(out + p, c->data, c->nc);
        break;
    case ISO7816_APDU_CASE_3E:
        out[p++] = 0x00;
        out[p++] = (uint8_t)((c->nc & 0xFF00) >> 8);
        out[p++] = (uint8_t)(c->nc & 0xFF);
        memcpy(out + p, c->data, c->nc);
        break;
    case ISO7816_APDU_CASE_4S:
        out[p++] = (uint8_t)c->nc;
        memcpy(out + p, c->data, c->nc);
        p += c->nc;
        out[p++] = (uint8_t)(c->ne >= 256 ? 0 : c->ne);
        break;
    case ISO7816_APDU_CASE_4E:
        out[p++] = 0x00;
        out[p++] = (uint8_t)((c->nc & 0xFF00) >> 8);
        out[p++] = (uint8_t)(c->nc & 0xFF);
        memcpy(out + p, c->data, c->nc);
        p += c->nc;
        out[p++] = (uint8_t)((c->ne & 0xFF00) >> 8);
        out[p++] = (uint8_t)(c->ne & 0xFF);
        break;
    }
    return n;
}