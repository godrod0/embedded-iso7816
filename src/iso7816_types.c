#include "iso7816/iso7816_types.h"

#include <stddef.h>

/* Tableau 7 (cl. 8.3) : code -> (Fi, fmax MHz) */
static const struct { uint8_t code; uint16_t fi; uint8_t fmax; } fi_tab[] = {
    {0x0, 372, 4}, {0x1, 372, 5}, {0x2, 558, 6}, {0x3, 744, 8},
    {0x4, 1116, 12}, {0x5, 1488, 16}, {0x6, 1860, 20},
    {0x9, 512, 5}, {0xA, 768, 7}, {0xB, 1024, 10}, {0xC, 1536, 15}, {0xD, 2048, 20},
};

/* Tableau 8 (cl. 8.3) : code -> Di */
static const struct { uint8_t code; uint8_t di; } di_tab[] = {
    {0x1, 1}, {0x2, 2}, {0x3, 4}, {0x4, 8}, {0x5, 16},
    {0x6, 32}, {0x7, 64}, {0x8, 12}, {0x9, 20},
};

bool iso7816_fi_from_code(uint8_t code, uint16_t *fi, uint8_t *fmax_mhz)
{
    for (size_t i = 0; i < sizeof(fi_tab) / sizeof(fi_tab[0]); i++) {
        if (fi_tab[i].code == code) {
            if (fi) *fi = fi_tab[i].fi;
            if (fmax_mhz) *fmax_mhz = fi_tab[i].fmax;
            return true;
        }
    }
    return false;
}

uint8_t iso7816_fi_to_code(uint16_t fi, uint8_t fmax_mhz, bool *found)
{
    if (found) *found = false;
    for (size_t i = 0; i < sizeof(fi_tab) / sizeof(fi_tab[0]); i++) {
        if (fi_tab[i].fi == fi && fi_tab[i].fmax == fmax_mhz) {
            if (found) *found = true;
            return fi_tab[i].code;
        }
    }
    /* fmax est indicatif : on accepte le code pour le Fi demandé */
    for (size_t i = 0; i < sizeof(fi_tab) / sizeof(fi_tab[0]); i++) {
        if (fi_tab[i].fi == fi) {
            if (found) *found = true;
            return fi_tab[i].code;
        }
    }
    return 0;
}

bool iso7816_di_from_code(uint8_t code, uint8_t *di)
{
    for (size_t i = 0; i < sizeof(di_tab) / sizeof(di_tab[0]); i++) {
        if (di_tab[i].code == code) {
            if (di) *di = di_tab[i].di;
            return true;
        }
    }
    return false;
}

uint8_t iso7816_di_to_code(uint8_t di, bool *found)
{
    if (found) *found = false;
    for (size_t i = 0; i < sizeof(di_tab) / sizeof(di_tab[0]); i++) {
        if (di_tab[i].di == di) {
            if (found) *found = true;
            return di_tab[i].code;
        }
    }
    return 0;
}