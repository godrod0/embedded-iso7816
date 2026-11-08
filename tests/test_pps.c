#include "iso7816/iso7816.h"
#include "iso7816/iso7816_char.h"
#include "iso7816/iso7816_pps.h"

#include "pc_card.h"
#include "test_main.h"

/* T0=0x10 (TA1 présent), TA1=0x16 -> Fi code 1 (372, 5 MHz), Di code 6 (Di=32) */
static const uint8_t ATR_TA1[] = { 0x10, 0x16 };

int main(void)
{
    /* --- PPS accepté : D passe de 1 à 8 --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_atr(card, ISO7816_ATR_TS_DIRECT, ATR_TA1, sizeof(ATR_TA1));
        pc_card_set_protocol(card, 0);
        pc_card_set_pps_accept(card, true);

        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
        CHECK_EQ(dev.atr.di, 32);

        CHECK_EQ(iso7816_pps(&dev, 0, 372, 8), ISO7816_OK);
        CHECK_EQ(dev.D, 8);
        CHECK_EQ(dev.F, 372);
        CHECK_EQ(iso7816_get_protocol(&dev), 0);
        CHECK_EQ(iso7816_etu_us(&dev), ISO7816_F_DEFAULT / 8);   /* 46 µs */

        /* la carte a reçu la requête PPS : FF 10 14 FB */
        uint8_t rx_log[16];
        size_t rx_len = pc_card_rxlog(card, rx_log, sizeof(rx_log));
        CHECK(rx_len >= 4);
        CHECK_EQ(rx_log[rx_len - 4], ISO7816_PPS_PPSS);
        CHECK_EQ(rx_log[rx_len - 3], ISO7816_PPS0_PPS1_BIT);   /* PPS0 : bit5=1 (PPS1 présent), T=0 */
        CHECK_EQ(rx_log[rx_len - 2], 0x14);   /* PPS1 : Fi code 1, Di code 4 (D=8) */
        CHECK_EQ(rx_log[rx_len - 1], ISO7816_PPS_PPSS ^ ISO7816_PPS0_PPS1_BIT ^ 0x14);

        CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
        pc_card_destroy(card);
    }

    /* --- PPS refusé par la carte (pas de réponse) -> PPS_FAILED --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_atr(card, ISO7816_ATR_TS_DIRECT, ATR_TA1, sizeof(ATR_TA1));
        pc_card_set_protocol(card, 0);
        pc_card_set_pps_accept(card, false);

        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);

        CHECK_EQ(iso7816_pps(&dev, 0, 372, 8), ISO7816_PPS_FAILED);

        CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
        pc_card_destroy(card);
    }

    /* --- protocole non offert -> PPS_PROTOCOL --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_atr(card, ISO7816_ATR_TS_DIRECT, ATR_TA1, sizeof(ATR_TA1));   /* offre T=0 seulement */
        pc_card_set_protocol(card, 0);

        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);

        CHECK_EQ(iso7816_pps(&dev, 1, 372, 1), ISO7816_PPS_PROTOCOL);

        CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
        pc_card_destroy(card);
    }

    TEST_END();
}