/*
 * iso7816_types.h — tables F/D et conversions (cl. 8.3, tableaux 7 et 8).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Table 7 : code (bits 8..5 de TA1 / PPS1) -> Fi et f(max) en MHz */
bool     iso7816_fi_from_code(uint8_t code, uint16_t *fi, uint8_t *fmax_mhz);
uint8_t  iso7816_fi_to_code(uint16_t fi, uint8_t fmax_mhz, bool *found);

/* Table 8 : code (bits 4..1 de TA1 / PPS1) -> Di */
bool     iso7816_di_from_code(uint8_t code, uint8_t *di);
uint8_t  iso7816_di_to_code(uint8_t di, bool *found);

#ifdef __cplusplus
}
#endif