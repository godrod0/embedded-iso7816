/*
 * iso7816_const.h — constantes de la norme ISO/IEC 7816-3:2006.
 *
 * Toute valeur littérale utilisée dans la bibliothèque est définie ici
 * sous forme de macro (une macro par constante), organisée par clause.
 */
#pragma once

/* ---- conversions et protocoles ---- */
#define ISO7816_US_PER_SECOND   1000000ULL  /* µs dans une seconde */
#define ISO7816_US_PER_MS       1000U       /* µs dans une milliseconde */
#define ISO7816_BITS_PER_BYTE   8U
#define ISO7816_PROTOCOL_T0     0U          /* T=0 */
#define ISO7816_PROTOCOL_T1     1U          /* T=1 */
#define ISO7816_PROTOCOL_NONE   0xFFU       /* aucun protocole retenu */
#define ISO7816_CLASS_COUNT     3U          /* classes A, B, C */

/* ---- cl. 5-6 : procédure carte (côté IFD) ---- */
#define ISO7816_CLK_RESET_MIN_HZ          1000000U  /* horloge min pendant reset/ATR (cl. 5.2.3) */
#define ISO7816_CLK_RESET_MAX_HZ          5000000U  /* horloge max pendant reset/ATR (cl. 5.2.3) */
#define ISO7816_DEFAULT_TIMEOUT_MS        1000U     /* timeout global de secours */
#define ISO7816_DEFAULT_MAX_RETRIES       3U        /* répétitions de caractère (cl. 7.3) */
#define ISO7816_COLD_RESET_RST_LOW_CYCLES 400U      /* RST à L >= 400 cycles (cl. 6.2.2) */
#define ISO7816_RESET_MARGIN_CYCLES       10U       /* marge ajoutée aux durées de reset */
#define ISO7816_RESET_ANSWER_MAX_CYCLES   40000U    /* réponse entre 400 et 40 000 cycles (cl. 6.2.2) */
#define ISO7816_CLASS_SWITCH_DELAY_US     10000U    /* >= 10 ms entre deux classes (cl. 6.2.4) */
#define ISO7816_WARM_RESET_MIN_CYCLES     4464U     /* warm reset pas avant 4464 cycles après T0 (cl. 6.2.3) */
#define ISO7816_CLOCK_STOP_IO_HIGH_CYCLES 1860U     /* I/O à H >= 1860 cycles avant arrêt CLK (cl. 6.3.2) */
#define ISO7816_CLOCK_RESTART_MIN_CYCLES  700U      /* reprise CLK >= 700 cycles (cl. 6.3.2) */
#define ISO7816_WT_ATR_PPS_ETU            9600U     /* WT pendant ATR et PPS (cl. 8.1, 9.1) */

/* Y de TA15 : masque des classes supportées (tableau 10) */
#define ISO7816_T15_CLASS_A_MASK 0x01U
#define ISO7816_T15_CLASS_B_MASK 0x02U
#define ISO7816_T15_CLASS_C_MASK 0x04U

/* ---- cl. 7 : trame caractère asynchrone ---- */
#define ISO7816_CHAR_MOMENT_START        1U   /* moment du start bit */
#define ISO7816_CHAR_MOMENT_FIRST_DATA   2U   /* premier moment de donnée */
#define ISO7816_CHAR_MOMENT_LAST_DATA    9U   /* dernier moment de donnée */
#define ISO7816_CHAR_MOMENT_PARITY       10U  /* moment de parité */
#define ISO7816_CHAR_MOMENT_TOTAL        10U  /* 10 moments au total */
#define ISO7816_CHAR_HALF_ETU_DIV        2U   /* demi-etu : échantillon à (m - 0,5) etu */
#define ISO7816_CHAR_GT_ETU              12U  /* guard time de base (cl. 7.2, 8.3) */
#define ISO7816_CHAR_ERR_DETECT_ETU      11U  /* détection signal d'erreur à (11 ± 0,2) etu (cl. 7.3) */
#define ISO7816_CHAR_ERR_RETRY_ETU       13U  /* retransmission >= 2 etu après détection (cl. 7.3) */
#define ISO7816_CHAR_ERR_SIGNAL_HALF_ETU 21U /* impulsion d'erreur à 10,5 etu = 21/2 etu (cl. 7.3) */
#define ISO7816_CHAR_ERR_PULSE_END_ETU   12U  /* fin de l'impulsion d'erreur (~1,5 etu) */
#define ISO7816_GUARD_N_NONE             0U   /* TC1 : pas de garde supplémentaire */
#define ISO7816_GUARD_N_SPECIAL          255U /* N=255 : GT = 12 etu (cl. 8.3) */

/* ---- cl. 8 : ATR ---- */
#define ISO7816_ATR_TS_DIRECT   0x3BU   /* convention directe */
#define ISO7816_ATR_TS_INVERSE  0x3FU   /* convention inverse */
#define ISO7816_ATR_MAX_BYTES   32U     /* TS + T0 + ... + TCK : 32 caractères max (cl. 8.1) */
#define ISO7816_ATR_HIST_MAX    15U
#define ISO7816_ATR_PROTO_MAX   8U
#define ISO7816_ATR_T0_K_MASK   0x0FU   /* K = T0 bits 4..1 */
#define ISO7816_ATR_YI_SHIFT    4U      /* Yi = T0/TDi bits 8..5 */
#define ISO7816_ATR_YI_TA_BIT   0x1U    /* présence de TAi */
#define ISO7816_ATR_YI_TB_BIT   0x2U    /* présence de TBi */
#define ISO7816_ATR_YI_TC_BIT   0x4U    /* présence de TCi */
#define ISO7816_ATR_YI_TD_BIT   0x8U    /* présence de TDi */
#define ISO7816_ATR_TD_PROTO_MASK 0x0FU /* T = TDi bits 4..1 */
#define ISO7816_ATR_T15         15U     /* T=15 (paramètres globaux) */
/* valeurs par défaut (cl. 8.3) */
#define ISO7816_F_DEFAULT        372U  /* Fd */
#define ISO7816_D_DEFAULT        1U    /* Dd */
#define ISO7816_FMAX_DEFAULT_MHZ 5U
#define ISO7816_GUARD_N_DEFAULT  0U
#define ISO7816_WI_DEFAULT       10U   /* TC2 par défaut (cl. 10.2) */
#define ISO7816_IFSC_DEFAULT     32U
#define ISO7816_CWI_DEFAULT      13U
#define ISO7816_BWI_DEFAULT      4U
#define ISO7816_IFSD_DEFAULT     32U
/* TA1 : Fi = bits 8..5, Di = bits 4..1 */
#define ISO7816_TA1_FI_SHIFT 4U
#define ISO7816_TA1_DI_MASK  0x0FU
#define ISO7816_TA2_PROTO_MASK 0x0FU   /* TA2 : mode spécifique, bits 4..1 */
/* TA15 : X = bits 8..7 (indicateur clock stop, tableau 9), Y = bits 6..1 (classes) */
#define ISO7816_TA15_X_SHIFT 6U
#define ISO7816_TA15_X_NONE     0U  /* clock stop non supporté */
#define ISO7816_TA15_X_LOW      1U  /* CLK arrêté à L */
#define ISO7816_TA15_X_HIGH     2U  /* CLK arrêté à H */
#define ISO7816_TA15_X_NO_PREF  3U  /* sans préférence */
#define ISO7816_TA15_Y_MASK  0x3FU
#define ISO7816_TB15_SPU_STANDARD_MASK 0x80U /* TB15 bit 8 = 0 : SPU usage standard */
/* TB1 spécifique T=1 : BWI = bits 8..5, CWI = bits 4..1 */
#define ISO7816_TB1_BWI_SHIFT 4U
#define ISO7816_TB1_CWI_MASK  0x0FU
#define ISO7816_TC1_CRC_BIT   0x01U    /* TC1 spécifique T=1, bit 1 : CRC au lieu de LRC */

/* ---- cl. 9 : PPS ---- */
#define ISO7816_PPS_PPSS         0xFFU  /* PPSS = 'FF' */
#define ISO7816_PPS_REQ_LEN      4U     /* PPSS, PPS0, PPS1, PCK */
#define ISO7816_PPS_RESP_MAX     6U     /* PPSS, PPS0..PPS3, PCK */
#define ISO7816_PPS0_PROTO_MASK  0x0FU  /* PPS0 bits 4..1 : type T retenu */
#define ISO7816_PPS0_PPS1_BIT    0x10U
#define ISO7816_PPS0_PPS2_BIT    0x20U
#define ISO7816_PPS0_PPS3_BIT    0x40U

/* ---- cl. 10 : T=0 ---- */
#define ISO7816_T0_HEADER_LEN     5U     /* CLA INS P1 P2 P3 */
#define ISO7816_T0_NULL           0x60U  /* octet de procédure NULL (cl. 10.3.3) */
#define ISO7816_T0_ACK_INVERT_MASK 0xFFU /* ACK partiel = INS ^ 0xFF */
#define ISO7816_T0_WT_ETU_PER_WI  960U   /* WT = WI * 960 * Fi/f (cl. 10.2) */
#define ISO7816_T0_MAX_6C_RETRIES 8U     /* réémissions '6Cxx' max (cl. 12.2) */
#define ISO7816_T0_NE_256         256U   /* Le = 0 -> 256 octets attendus */

/* SW1-SW2 (cl. 10.3.3, 12.2) */
#define ISO7816_SW_LEN           2U     /* SW1-SW2 = 2 octets */
#define ISO7816_SW1_MASK         0xFF00U
#define ISO7816_SW2_MASK         0x00FFU
#define ISO7816_SW1_NIBBLE_MASK  0xF0U
#define ISO7816_SW1_6X           0x60U  /* '6X' (hors '60') : état intermediaire */
#define ISO7816_SW1_9X           0x90U  /* '9X' */
#define ISO7816_SW_OK            0x9000U
#define ISO7816_SW1_GET_RESPONSE 0x61U  /* '61xx' : données restantes via GET RESPONSE */
#define ISO7816_SW1_RETRANSMIT   0x6CU  /* '6Cxx' : réémettre avec P3 = SW2 */
#define ISO7816_INS_GET_RESPONSE 0xC0U
#define ISO7816_GET_RESPONSE_CLA 0x00U

/* ---- cl. 11 : T=1 ---- */
#define ISO7816_T1_INF_MAX          254U /* INF max (LEN sur 1 octet, hors 255 réservé) */
#define ISO7816_T1_HEADER_LEN       3U   /* NAD + PCB + LEN */
#define ISO7816_T1_NAD_DEFAULT      0x00U
#define ISO7816_T1_BGT_ETU          22U  /* block guard time (cl. 11.2) */
#define ISO7816_T1_CWT_BASE_ETU     11U  /* CWT = (11 + 2^CWI) etu (cl. 11.4.3) */
#define ISO7816_T1_BWT_BASE_ETU     11U  /* BWT = 11 etu + 2^BWI * 960 * Fd/f (cl. 11.4.3) */
#define ISO7816_T1_BWT_FD           372U /* Fd */
#define ISO7816_T1_BWT_ETU_PER_BWI  960U
/* CRC-16 ISO/IEC 13239 (cl. 11.3.4) */
#define ISO7816_T1_CRC16_POLY     0x1021U
#define ISO7816_T1_CRC16_INIT     0xFFFFU
#define ISO7816_T1_CRC16_MSB_MASK 0x8000U
/* PCB (cl. 11.3.2) : le type se lit sur le bit 8 (0 = I-bloc) et les bits 8..7 (R/S) */
#define ISO7816_T1_PCB_IBLOCK_BIT 0x80U /* bit 8 : 0 -> I-bloc */
#define ISO7816_T1_PCB_TYPE_MASK  0xC0U /* bits 8..7 : type de bloc */
#define ISO7816_T1_PCB_RBLOCK     0x80U /* R-bloc : bits 8..7 = 10 */
#define ISO7816_T1_PCB_SBLOCK     0xC0U /* S-bloc : bits 8..7 = 11 */
#define ISO7816_T1_PCB_NS_BIT     0x40U /* I-bloc : bit 7 = N(S) */
#define ISO7816_T1_PCB_MORE_BIT   0x20U /* I-bloc : bit 6 = M (chaînage) */
#define ISO7816_T1_PCB_NR_BIT     0x20U /* R-bloc : bit 6 = N(R) */
#define ISO7816_T1_PCB_RESP_BIT   0x20U /* S-bloc : bit 6 = 1 -> réponse */
#define ISO7816_T1_PCB_FUNC_MASK  0x1FU /* bits 5..1 : fonction S ou code erreur R */
/* fonctions S (cl. 11.3.2.2) */
#define ISO7816_T1_S_RESYNCH 0x00U
#define ISO7816_T1_S_IFS     0x01U
#define ISO7816_T1_S_ABORT   0x02U
#define ISO7816_T1_S_WTX     0x03U
/* codes erreur R (cl. 11.3.2.2) */
#define ISO7816_T1_R_ERR_EDC   0x01U /* erreur LRC/CRC ou parité */
#define ISO7816_T1_R_ERR_OTHER 0x02U /* autre erreur (séquence...) */
#define ISO7816_T1_RESYNC_MAX      3U  /* tentatives de RESYNCH max (règle 6.4) */
#define ISO7816_T1_MAX_RETRIES     2   /* retries max avant RESYNCH (règles 7.1/7.4) */
#define ISO7816_T1_MAX_GET_RESPONSE 16U /* boucles GET RESPONSE '61xx' max */
#define ISO7816_T1_SW2_ALL         256U /* SW2 = 0 -> 256 octets */

/* ---- cl. 12.1 : APDU ---- */
#define ISO7816_APDU_HEADER_LEN  4U   /* CLA INS P1 P2 */
#define ISO7816_APDU_CASE1_LEN   4U
#define ISO7816_APDU_CASE2S_LEN  5U
#define ISO7816_APDU_CASE2E_LEN  7U
#define ISO7816_APDU_LC_OFFSET_SHORT   4U  /* cas court : Lc à l'octet 4 (après l'en-tête) */
#define ISO7816_APDU_DATA_OFFSET_SHORT 5U  /* cas court : data à l'octet 5 */
#define ISO7816_APDU_DATA_OFFSET_EXT   7U  /* cas étendu : data à l'octet 7 */
#define ISO7816_APDU_NC_MAX_SHORT 255U
#define ISO7816_APDU_NC_MAX_EXT   65535U
#define ISO7816_APDU_NE_256       256U   /* Le = 0 -> 256 (cas court) */
#define ISO7816_APDU_NE_65536     65536U /* Le = 0x0000 -> 65536 (cas étendu) */
#define ISO7816_APDU_EXTENDED_MARKER 0x00U /* Lc octet 5 = 0 -> Lc sur 2 octets */
#define ISO7816_BYTE_MASK      0x00FFU
#define ISO7816_BYTE_HIGH_MASK 0xFF00U

/* ---- port STM32U5 ---- */
#define ISO7816_PORT_TIME_BASE_HZ   1000000U /* base de temps µs : TIM à 1 MHz */
#define ISO7816_PORT_TIM_PERIOD     0xFFFFU  /* période max du timer de base de temps */
#define ISO7816_PORT_CLK_RES_FACTOR 256U     /* PSC = f_TIM / (hz * 256) pour ARR petit */
#define ISO7816_PORT_PWM_DUTY_DIV   2U       /* rapport cyclique 50 % */
#define ISO7816_PORT_IO_PULLUP_OHM  20000U   /* pull-up externe I/O = 20 kΩ (cl. 5.2.5) */
