#include "iso7816/iso7816.h"

#include <string.h>

#include "pc_card.h"
#include "test_main.h"

/* ATR T=1 seul : T0=0x80, TD1=0x01, TCK=0x81 */
static const uint8_t ATR_T1[] = { 0x80, 0x01, 0x81 };

/* ATR T=1 avec IFSC=8 : T0=0x80, TD1=0x11, TA(T=1)=0x08, TCK=0x99 */
static const uint8_t ATR_T1_IFSC8[] = { 0x80, 0x11, 0x08, 0x99 };

static void h_read(pc_card_t *c, const uint8_t *apdu, size_t len)
{
    (void)apdu; (void)len;
    static const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    pc_card_t1_queue_response(c, data, sizeof(data), 0x9000);
}

static void h_read_big(pc_card_t *c, const uint8_t *apdu, size_t len)
{
    (void)apdu; (void)len;
    static uint8_t data[20];
    for (int i = 0; i < 20; i++) data[i] = (uint8_t)i;
    pc_card_t1_queue_response(c, data, sizeof(data), 0x9000);
}

static uint8_t g_apdu[64];
static size_t g_apdu_len;
static void h_update(pc_card_t *c, const uint8_t *apdu, size_t len)
{
    if (len <= sizeof(g_apdu)) {
        memcpy(g_apdu, apdu, len);
        g_apdu_len = len;
    }
    pc_card_t1_queue_response(c, NULL, 0, 0x9000);
}

int main(void)
{
    /* --- échange simple T=1 (LRC) --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_atr(card, 0x3B, ATR_T1, sizeof(ATR_T1));
        pc_card_set_protocol(card, 1);
        pc_card_set_cmd_handler(card, 0xB0, false, h_read);

        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = 1000000 };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
        CHECK_EQ(iso7816_get_protocol(&dev), 1);
        CHECK(!dev.use_error_signal);   /* pas de signal d'erreur en T=1 (cl. 11.2) */

        uint8_t resp[64];
        size_t rlen = 0;
        uint16_t sw = 0;
        uint8_t cmd[] = { 0x00, 0xB0, 0x00, 0x00, 0x04 };
        CHECK_EQ(iso7816_exchange(&dev, cmd, sizeof(cmd), resp, sizeof(resp), &rlen, &sw), ISO7816_OK);
        CHECK_EQ(sw, 0x9000);
        CHECK_EQ(rlen, 4);
        CHECK_EQ(resp[0], 0xDE);
        CHECK_EQ(resp[3], 0xEF);

        CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
        pc_card_destroy(card);
    }

    /* --- réponse chaînée (IFSC=8, 20 octets -> 3 I-blocs) + commande chaînée --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_atr(card, 0x3B, ATR_T1_IFSC8, sizeof(ATR_T1_IFSC8));
        pc_card_set_protocol(card, 1);
        pc_card_set_cmd_handler(card, 0xB0, false, h_read_big);
        pc_card_set_cmd_handler(card, 0xD0, true, h_update);

        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = 1000000 };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
        CHECK_EQ(dev.ifsc, 8);

        uint8_t resp[64];
        size_t rlen = 0;
        uint16_t sw = 0;

        /* cas 2S, Ne=20 : la réponse arrive en blocs chaînés (M-bit) */
        uint8_t cmd[] = { 0x00, 0xB0, 0x00, 0x00, 0x14 };
        CHECK_EQ(iso7816_exchange(&dev, cmd, sizeof(cmd), resp, sizeof(resp), &rlen, &sw), ISO7816_OK);
        CHECK_EQ(sw, 0x9000);
        CHECK_EQ(rlen, 20);
        for (int i = 0; i < 20; i++) CHECK_EQ(resp[i], i);

        /* cas 3S avec Nc=20 > IFSC=8 : la commande part en blocs chaînés */
        uint8_t cmd3[25];
        cmd3[0] = 0x00; cmd3[1] = 0xD0; cmd3[2] = 0x00; cmd3[3] = 0x00; cmd3[4] = 20;
        for (int i = 0; i < 20; i++) cmd3[5 + i] = (uint8_t)(0x40 + i);
        CHECK_EQ(iso7816_exchange(&dev, cmd3, sizeof(cmd3), resp, sizeof(resp), &rlen, &sw), ISO7816_OK);
        CHECK_EQ(sw, 0x9000);

        /* la carte a reçu l'APDU complète (header 5 + 20 data) malgré le chaînage */
        CHECK_EQ(g_apdu_len, 25);
        CHECK_EQ(g_apdu[0], 0x00);
        CHECK_EQ(g_apdu[1], 0xD0);
        CHECK_EQ(g_apdu[4], 20);
        CHECK_EQ(g_apdu[5], 0x40);
        CHECK_EQ(g_apdu[24], 0x40 + 19);

        CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
        pc_card_destroy(card);
    }

    TEST_END();
}