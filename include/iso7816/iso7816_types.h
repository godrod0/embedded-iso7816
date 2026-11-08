/**
 * @file iso7816_types.h
 * @brief F/D tables and conversions (cl. 8.3, tables 7 and 8).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Table 7: code (bits 8..5 of TA1 / PPS1) -> Fi and f(max) in MHz.
 * @param code     Fi code.
 * @param fi       Receives Fi (NULL allowed).
 * @param fmax_mhz Receives f(max) in MHz (NULL allowed).
 * @return true if the code is known.
 */
bool     iso7816_fi_from_code(uint8_t code, uint16_t *fi, uint8_t *fmax_mhz);

/**
 * @brief Table 7: Fi (+ indicative fmax) -> code.
 * @param fi       Fi value.
 * @param fmax_mhz Indicative f(max) in MHz.
 * @param found    Receives true if a code was found (NULL allowed).
 * @return The code, or 0 if not found.
 */
uint8_t  iso7816_fi_to_code(uint16_t fi, uint8_t fmax_mhz, bool *found);

/**
 * @brief Table 8: code (bits 4..1 of TA1 / PPS1) -> Di.
 * @param code Code.
 * @param di   Receives Di (NULL allowed).
 * @return true if the code is known.
 */
bool     iso7816_di_from_code(uint8_t code, uint8_t *di);

/**
 * @brief Table 8: Di -> code.
 * @param di    Di value.
 * @param found Receives true if a code was found (NULL allowed).
 * @return The code, or 0 if not found.
 */
uint8_t  iso7816_di_to_code(uint8_t di, bool *found);

#ifdef __cplusplus
}
#endif
