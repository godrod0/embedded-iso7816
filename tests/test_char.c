#include "iso7816/iso7816.h"
#include "iso7816/iso7816_char.h"

#include "pc_card.h"
#include "test_main.h"

/* ATR minimal : T0 = 0x00 -> T=0 par défaut */
static const uint8_t ATR_SIMPLE[] = { 0x00 };

int main(void)
{
    pc_card_t *card = pc_card_create();
    pc_card_set_atr(card, ISO7816_ATR_TS_DIRECT, ATR_SIMPLE, sizeof(ATR_SIMPLE));
    pc_card_set_protocol(card, 0);

    iso7816_t dev;
    iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ, .timeout_ms = ISO7816_DEFAULT_TIMEOUT_MS, .max_retries = ISO7816_DEFAULT_MAX_RETRIES };
    CHECK_EQ(iso7816_init(&dev, &cfg.hal ? pc_card_hal(card) : NULL, &cfg), ISO7816_OK);
    pc_card_attach(card, &dev);

    CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
    CHECK(dev.active);
    CHECK_EQ(iso7816_get_protocol(&dev), 0);
    CHECK_EQ(iso7816_etu_us(&dev), ISO7816_F_DEFAULT);   /* F=372, D=1, f=1 MHz */

    /* émission d'un caractère vers la carte : la carte sim doit le recevoir */
    {
        uint8_t rx_log[16];
        size_t rx_len_before = pc_card_rxlog(card, rx_log, sizeof(rx_log));
        CHECK_EQ(iso7816_char_tx(&dev, 0xA4), ISO7816_OK);
        uint8_t rx_log2[16];
        size_t rx_len_after = pc_card_rxlog(card, rx_log2, sizeof(rx_log2));
        CHECK_EQ(rx_len_after, rx_len_before + 1);
        CHECK_EQ(rx_log2[rx_len_after - 1], 0xA4);
    }

    /* réception d'un caractère émis par la carte */
    {
        uint8_t byte = 0xFF;
        uint8_t send[] = { 0x5A };
        pc_card_queue_tx(card, send, 1);
        CHECK_EQ(iso7816_char_rx(&dev, &byte, ISO7816_WT_ATR_PPS_ETU * iso7816_etu_us(&dev)), ISO7816_OK);
        CHECK_EQ(byte, 0x5A);
    }

    /* parité fausse côté carte -> l'IFD émet le signal d'erreur (cl. 7.3) */
    {
        uint8_t byte = 0;
        uint8_t send[] = { 0x5A };
        pc_card_corrupt_next_tx(card);
        pc_card_queue_tx(card, send, 1);
        CHECK_EQ(iso7816_char_rx(&dev, &byte, ISO7816_WT_ATR_PPS_ETU * iso7816_etu_us(&dev)), ISO7816_CHAR_PARITY);
        CHECK(pc_card_ifd_err_signals(card) >= 1);
    }

    /* timeout WT : carte muette */
    {
        uint8_t byte;
        CHECK_EQ(iso7816_char_rx(&dev, &byte, ISO7816_T0_WT_ETU_PER_WI * iso7816_etu_us(&dev)), ISO7816_CHAR_WT);
    }

    CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
    CHECK(!dev.active);

    pc_card_destroy(card);
    TEST_END();
}