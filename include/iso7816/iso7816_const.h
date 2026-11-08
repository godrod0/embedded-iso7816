/**
 * @file iso7816_const.h
 * @brief Constants of ISO/IEC 7816-3:2006.
 *
 * Every literal used in the library is defined here as a macro
 * (one macro per constant), grouped by clause.
 */
#pragma once

/** @name Conversions and protocols */
/** @{ */
#define ISO7816_US_PER_SECOND   1000000ULL  /**< µs in one second */
#define ISO7816_US_PER_MS       1000U       /**< µs in one millisecond */
#define ISO7816_BITS_PER_BYTE   8U
#define ISO7816_PROTOCOL_T0     0U          /**< T=0 */
#define ISO7816_PROTOCOL_T1     1U          /**< T=1 */
#define ISO7816_PROTOCOL_NONE   0xFFU       /**< No protocol selected */
#define ISO7816_CLASS_COUNT     3U          /**< Classes A, B, C */
/** @} */

/** @name cl. 5-6: card procedure (IFD side) */
/** @{ */
#define ISO7816_CLK_RESET_MIN_HZ          1000000U  /**< Min clock during reset/ATR (cl. 5.2.3) */
#define ISO7816_CLK_RESET_MAX_HZ          5000000U  /**< Max clock during reset/ATR (cl. 5.2.3) */
#define ISO7816_DEFAULT_TIMEOUT_MS        1000U     /**< Fallback global timeout */
#define ISO7816_DEFAULT_MAX_RETRIES       3U        /**< Character retransmissions (cl. 7.3) */
#define ISO7816_COLD_RESET_RST_LOW_CYCLES 400U      /**< RST low >= 400 cycles (cl. 6.2.2) */
#define ISO7816_RESET_MARGIN_CYCLES       10U       /**< Margin added to reset durations */
#define ISO7816_RESET_ANSWER_MAX_CYCLES   40000U    /**< Answer between 400 and 40 000 cycles (cl. 6.2.2) */
#define ISO7816_CLASS_SWITCH_DELAY_US     10000U    /**< >= 10 ms between two classes (cl. 6.2.4) */
#define ISO7816_WARM_RESET_MIN_CYCLES     4464U     /**< Warm reset not before 4464 cycles after T0 (cl. 6.2.3) */
#define ISO7816_CLOCK_STOP_IO_HIGH_CYCLES 1860U     /**< I/O high >= 1860 cycles before clock stop (cl. 6.3.2) */
#define ISO7816_CLOCK_RESTART_MIN_CYCLES  700U      /**< Clock restart >= 700 cycles (cl. 6.3.2) */
#define ISO7816_WT_ATR_PPS_ETU            9600U     /**< WT during ATR and PPS (cl. 8.1, 9.1) */
/** @} */

/** @name TA15 Y: supported classes mask (table 10) */
/** @{ */
#define ISO7816_T15_CLASS_A_MASK 0x01U
#define ISO7816_T15_CLASS_B_MASK 0x02U
#define ISO7816_T15_CLASS_C_MASK 0x04U
/** @} */

/** @name cl. 7: asynchronous character frame */
/** @{ */
#define ISO7816_CHAR_MOMENT_START        1U   /**< Start-bit moment */
#define ISO7816_CHAR_MOMENT_FIRST_DATA   2U   /**< First data moment */
#define ISO7816_CHAR_MOMENT_LAST_DATA    9U   /**< Last data moment */
#define ISO7816_CHAR_MOMENT_PARITY       10U  /**< Parity moment */
#define ISO7816_CHAR_MOMENT_TOTAL        10U  /**< Total moments */
#define ISO7816_CHAR_HALF_ETU_DIV        2U   /**< Half etu: sampling at (m - 0.5) etu */
#define ISO7816_CHAR_GT_ETU              12U  /**< Base guard time (cl. 7.2, 8.3) */
#define ISO7816_CHAR_ERR_DETECT_ETU      11U  /**< Error-signal detection at (11 ± 0.2) etu (cl. 7.3) */
#define ISO7816_CHAR_ERR_RETRY_ETU       13U  /**< Retransmission >= 2 etu after detection (cl. 7.3) */
#define ISO7816_CHAR_ERR_SIGNAL_HALF_ETU 21U /**< Error pulse at 10.5 etu = 21/2 etu (cl. 7.3) */
#define ISO7816_CHAR_ERR_PULSE_END_ETU   12U  /**< End of error pulse (~1.5 etu) */
#define ISO7816_GUARD_N_NONE             0U   /**< TC1: no extra guard */
#define ISO7816_GUARD_N_SPECIAL          255U /**< N=255: GT = 12 etu (cl. 8.3) */
/** @} */

/** @name cl. 8: ATR */
/** @{ */
#define ISO7816_ATR_TS_DIRECT   0x3BU   /**< Direct convention */
#define ISO7816_ATR_TS_INVERSE  0x3FU   /**< Inverse convention */
#define ISO7816_ATR_MAX_BYTES   32U     /**< TS + T0 + ... + TCK: 32 characters max (cl. 8.1) */
#define ISO7816_ATR_HIST_MAX    15U
#define ISO7816_ATR_PROTO_MAX   8U
#define ISO7816_ATR_T0_K_MASK   0x0FU   /**< K = T0 bits 4..1 */
#define ISO7816_ATR_YI_SHIFT    4U      /**< Yi = T0/TDi bits 8..5 */
#define ISO7816_ATR_YI_TA_BIT   0x1U    /**< TAi present */
#define ISO7816_ATR_YI_TB_BIT   0x2U    /**< TBi present */
#define ISO7816_ATR_YI_TC_BIT   0x4U    /**< TCi present */
#define ISO7816_ATR_YI_TD_BIT   0x8U    /**< TDi present */
#define ISO7816_ATR_TD_PROTO_MASK 0x0FU /**< T = TDi bits 4..1 */
#define ISO7816_ATR_T15         15U     /**< T=15 (global parameters) */
/* defaults (cl. 8.3) */
#define ISO7816_F_DEFAULT        372U  /**< Fd */
#define ISO7816_D_DEFAULT        1U    /**< Dd */
#define ISO7816_FMAX_DEFAULT_MHZ 5U
#define ISO7816_GUARD_N_DEFAULT  0U
#define ISO7816_WI_DEFAULT       10U   /**< TC2 default (cl. 10.2) */
#define ISO7816_IFSC_DEFAULT     32U
#define ISO7816_CWI_DEFAULT      13U
#define ISO7816_BWI_DEFAULT      4U
#define ISO7816_IFSD_DEFAULT     32U
/* TA1: Fi = bits 8..5, Di = bits 4..1 */
#define ISO7816_TA1_FI_SHIFT 4U
#define ISO7816_TA1_DI_MASK  0x0FU
#define ISO7816_TA2_PROTO_MASK 0x0FU   /**< TA2: specific mode, bits 4..1 */
/* TA15: X = bits 8..7 (clock-stop indicator, table 9), Y = bits 6..1 (classes) */
#define ISO7816_TA15_X_SHIFT 6U
#define ISO7816_TA15_X_NONE     0U  /**< Clock stop not supported */
#define ISO7816_TA15_X_LOW      1U  /**< CLK stopped at L */
#define ISO7816_TA15_X_HIGH     2U  /**< CLK stopped at H */
#define ISO7816_TA15_X_NO_PREF  3U  /**< No preference */
#define ISO7816_TA15_Y_MASK  0x3FU
#define ISO7816_TB15_SPU_STANDARD_MASK 0x80U /**< TB15 bit 8 = 0: SPU usage standard */
/* TB1 specific to T=1: BWI = bits 8..5, CWI = bits 4..1 */
#define ISO7816_TB1_BWI_SHIFT 4U
#define ISO7816_TB1_CWI_MASK  0x0FU
#define ISO7816_TC1_CRC_BIT   0x01U    /**< TC1 specific to T=1, bit 1: CRC instead of LRC */
/** @} */

/** @name cl. 9: PPS */
/** @{ */
#define ISO7816_PPS_PPSS         0xFFU  /**< PPSS = 'FF' */
#define ISO7816_PPS_REQ_LEN      4U     /**< PPSS, PPS0, PPS1, PCK */
#define ISO7816_PPS_RESP_MAX     6U     /**< PPSS, PPS0..PPS3, PCK */
#define ISO7816_PPS0_PROTO_MASK  0x0FU  /**< PPS0 bits 4..1: selected T */
#define ISO7816_PPS0_PPS1_BIT    0x10U
#define ISO7816_PPS0_PPS2_BIT    0x20U
#define ISO7816_PPS0_PPS3_BIT    0x40U
/** @} */

/** @name cl. 10: T=0 */
/** @{ */
#define ISO7816_T0_HEADER_LEN     5U     /**< CLA INS P1 P2 P3 */
#define ISO7816_T0_NULL           0x60U  /**< NULL procedure byte (cl. 10.3.3) */
#define ISO7816_T0_ACK_INVERT_MASK 0xFFU /**< Partial ACK = INS ^ 0xFF */
#define ISO7816_T0_WT_ETU_PER_WI  960U   /**< WT = WI * 960 * Fi/f (cl. 10.2) */
#define ISO7816_T0_MAX_6C_RETRIES 8U     /**< Max '6Cxx' retransmissions (cl. 12.2) */
#define ISO7816_T0_NE_256         256U   /**< Le = 0 -> 256 expected bytes */
/** @} */

/** @name SW1-SW2 (cl. 10.3.3, 12.2) */
/** @{ */
#define ISO7816_SW_LEN           2U     /**< SW1-SW2 = 2 bytes */
#define ISO7816_SW1_MASK         0xFF00U
#define ISO7816_SW2_MASK         0x00FFU
#define ISO7816_SW1_NIBBLE_MASK  0xF0U
#define ISO7816_SW1_6X           0x60U  /**< '6X' (except '60'): intermediate state */
#define ISO7816_SW1_9X           0x90U  /**< '9X' */
#define ISO7816_SW_OK            0x9000U
#define ISO7816_SW1_GET_RESPONSE 0x61U  /**< '61xx': remaining data via GET RESPONSE */
#define ISO7816_SW1_RETRANSMIT   0x6CU  /**< '6Cxx': retransmit with P3 = SW2 */
#define ISO7816_INS_GET_RESPONSE 0xC0U
#define ISO7816_GET_RESPONSE_CLA 0x00U
/** @} */

/** @name cl. 11: T=1 */
/** @{ */
#define ISO7816_T1_INF_MAX          254U /**< Max INF (1-byte LEN, 255 reserved) */
#define ISO7816_T1_HEADER_LEN       3U   /**< NAD + PCB + LEN */
#define ISO7816_T1_NAD_DEFAULT      0x00U
#define ISO7816_T1_BGT_ETU          22U  /**< Block guard time (cl. 11.2) */
#define ISO7816_T1_CWT_BASE_ETU     11U  /**< CWT = (11 + 2^CWI) etu (cl. 11.4.3) */
#define ISO7816_T1_BWT_BASE_ETU     11U  /**< BWT = 11 etu + 2^BWI * 960 * Fd/f (cl. 11.4.3) */
#define ISO7816_T1_BWT_FD           372U /**< Fd */
#define ISO7816_T1_BWT_ETU_PER_BWI  960U
/* CRC-16 ISO/IEC 13239 (cl. 11.3.4) */
#define ISO7816_T1_CRC16_POLY     0x1021U
#define ISO7816_T1_CRC16_INIT     0xFFFFU
#define ISO7816_T1_CRC16_MSB_MASK 0x8000U
/* PCB (cl. 11.3.2): type is read from bit 8 (0 = I-block) and bits 8..7 (R/S) */
#define ISO7816_T1_PCB_IBLOCK_BIT 0x80U /**< Bit 8: 0 -> I-block */
#define ISO7816_T1_PCB_TYPE_MASK  0xC0U /**< Bits 8..7: block type */
#define ISO7816_T1_PCB_RBLOCK     0x80U /**< R-block: bits 8..7 = 10 */
#define ISO7816_T1_PCB_SBLOCK     0xC0U /**< S-block: bits 8..7 = 11 */
#define ISO7816_T1_PCB_NS_BIT     0x40U /**< I-block: bit 7 = N(S) */
#define ISO7816_T1_PCB_MORE_BIT   0x20U /**< I-block: bit 6 = M (chaining) */
#define ISO7816_T1_PCB_NR_BIT     0x20U /**< R-block: bit 6 = N(R) */
#define ISO7816_T1_PCB_RESP_BIT   0x20U /**< S-block: bit 6 = 1 -> response */
#define ISO7816_T1_PCB_FUNC_MASK  0x1FU /**< Bits 5..1: S-function or R error code */
/* S-functions (cl. 11.3.2.2) */
#define ISO7816_T1_S_RESYNCH 0x00U
#define ISO7816_T1_S_IFS     0x01U
#define ISO7816_T1_S_ABORT   0x02U
#define ISO7816_T1_S_WTX     0x03U
/* R error codes (cl. 11.3.2.2) */
#define ISO7816_T1_R_ERR_EDC   0x01U /**< LRC/CRC or parity error */
#define ISO7816_T1_R_ERR_OTHER 0x02U /**< Other error (sequence...) */
#define ISO7816_T1_RESYNC_MAX      3U  /**< Max RESYNCH attempts (rule 6.4) */
#define ISO7816_T1_MAX_RETRIES     2   /**< Max retries before RESYNCH (rules 7.1/7.4) */
#define ISO7816_T1_MAX_GET_RESPONSE 16U /**< Max GET RESPONSE '61xx' loops */
#define ISO7816_T1_SW2_ALL         256U /**< SW2 = 0 -> 256 bytes */
/** @} */

/** @name cl. 12.1: APDU */
/** @{ */
#define ISO7816_APDU_HEADER_LEN  4U   /**< CLA INS P1 P2 */
#define ISO7816_APDU_CASE1_LEN   4U
#define ISO7816_APDU_CASE2S_LEN  5U
#define ISO7816_APDU_CASE2E_LEN  7U
#define ISO7816_APDU_LC_OFFSET_SHORT   4U  /**< Short case: Lc at byte 4 (after the header) */
#define ISO7816_APDU_DATA_OFFSET_SHORT 5U  /**< Short case: data at byte 5 */
#define ISO7816_APDU_DATA_OFFSET_EXT   7U  /**< Extended case: data at byte 7 */
#define ISO7816_APDU_NC_MAX_SHORT 255U
#define ISO7816_APDU_NC_MAX_EXT   65535U
#define ISO7816_APDU_NE_256       256U   /**< Le = 0 -> 256 (short case) */
#define ISO7816_APDU_NE_65536     65536U /**< Le = 0x0000 -> 65536 (extended case) */
#define ISO7816_APDU_EXTENDED_MARKER 0x00U /**< Lc byte 5 = 0 -> Lc on 2 bytes */
#define ISO7816_BYTE_MASK      0x00FFU
#define ISO7816_BYTE_HIGH_MASK 0xFF00U
/** @} */
