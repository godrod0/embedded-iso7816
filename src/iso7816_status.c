#include "iso7816/iso7816_status.h"

const char *iso7816_strstatus(iso7816_status_t stat)
{
    switch (stat) {
    case ISO7816_OK:               return "OK";
    case ISO7816_GEN_HAL:          return "GEN_HAL: erreur du port materiel";
    case ISO7816_GEN_PARAM:        return "GEN_PARAM: argument invalide";
    case ISO7816_GEN_STATE:        return "GEN_STATE: appel hors sequence";
    case ISO7816_GEN_SIZE:         return "GEN_SIZE: buffer trop petit";
    case ISO7816_DEV_NO_ANSWER:    return "DEV_NO_ANSWER: pas de reponse au reset (40000 cycles)";
    case ISO7816_DEV_CLASS:        return "DEV_CLASS: classe non supportee par la carte";
    case ISO7816_DEV_IO:           return "DEV_IO: conflit de pilotage I/O";
    case ISO7816_CHAR_PARITY:      return "CHAR_PARITY: parite incorrecte apres repetitions";
    case ISO7816_CHAR_WT:          return "CHAR_WT: waiting time depasse";
    case ISO7816_ATR_TS:           return "ATR_TS: caractere initial TS invalide";
    case ISO7816_ATR_MALFORMED:    return "ATR_MALFORMED: ATR malforme";
    case ISO7816_ATR_TCK:          return "ATR_TCK: TCK invalide";
    case ISO7816_PPS_FAILED:       return "PPS_FAILED: echange PPS non concluant";
    case ISO7816_PPS_PROTOCOL:     return "PPS_PROTOCOL: protocole non offert par la carte";
    case ISO7816_T0_PROC:          return "T0_PROC: octet de procedure invalide";
    case ISO7816_T1_EDC:           return "T1_EDC: erreur LRC/CRC persistante";
    case ISO7816_T1_SEQ:           return "T1_SEQ: erreur de sequence N(S)/N(R)";
    case ISO7816_T1_BWT:           return "T1_BWT: carte muette (BWT depasse)";
    case ISO7816_T1_RESYNC:        return "T1_RESYNC: resynchronisation echouee";
    case ISO7816_APDU_INVALID:     return "APDU_INVALID: command APDU invalide";
    }
    return "?";
}