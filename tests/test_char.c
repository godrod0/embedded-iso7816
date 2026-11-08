#include "iso7816/iso7816.h"
#include "iso7816/iso7816_char.h"

#include "pc_card.h"
#include "test_main.h"

/* ATR minimal : T0 = 0x00 -> T=0 par défaut */
static const uint8_t ATR_SIMPLE[] = { 0x00 };

int main(void)
{
    pc_card_t *card = pc_card_create();
    pc_card_set_atr(card, 0x3B, ATR_SIMPLE, sizeof(ATR_SIMPLE));
    pc_card_set_protocol(card, 0);

    iso7816_t dev;
    iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = 1000000, .timeout_ms = 1000, .max_retries = 3 };
    CHECK_EQ(iso7816_init(&dev, &cfg.hal ? pc_card_hal(card) : NULL, &cfg), ISO7816_OK);
    pc_card_attach(card, &dev);

    CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
    CHECK(dev.active);
    CHECK_EQ(iso7816_get_protocol(&dev), 0);
    CHECK_EQ(iso7816_etu_us(&dev), 372);   /* F=372, D=1, f=1 MHz */

    /* émission d'un caractère vers la carte : la carte sim doit le recevoir */
    {
        uint8_t log[16];
        size_t n0 = pc_card_rxlog(card, log, sizeof(log));
        CHECK_EQ(iso7816_char_tx(&dev, 0xA4), ISO7816_OK);
        uint8_t log2[16];
        size_t n1 = pc_card_rxlog(card, log2, sizeof(log2));
        CHECK_EQ(n1, n0 + 1);
        CHECK_EQ(log2[n1 - 1], 0xA4);
    }

    /* réception d'un caractère émis par la carte */
    {
        uint8_t b = 0xFF;
        uint8_t send[] = { 0x5A };
        pc_card_queue_tx(card, send, 1);
        CHECK_EQ(iso7816_char_rx(&dev, &b, 9600U * 372U), ISO7816_OK);
        CHECK_EQ(b, 0x5A);
    }

    /* parité fausse côté carte -> l'IFD émet le signal d'erreur (cl. 7.3) */
    {
        uint8_t b = 0;
        uint8_t send[] = { 0x5A };
        pc_card_corrupt_next_tx(card);
        pc_card_queue_tx(card, send, 1);
        CHECK_EQ(iso7816_char_rx(&dev, &b, 9600U * 372U), ISO7816_CHAR_PARITY);
        CHECK(pc_card_ifd_err_signals(card) >= 1);
    }

    /* timeout WT : carte muette */
    {
        uint8_t b;
        CHECK_EQ(iso7816_char_rx(&dev, &b, 960U * 372U), ISO7816_CHAR_WT);
    }

    CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
    CHECK(!dev.active);

    pc_card_destroy(card);
    TEST_END();
}