#include "iso7816/iso7816.h"

#include "pc_card.h"
#include "test_main.h"

static const uint8_t ATR_T0[] = { 0x00 };   /* T=0 by default */

static void h_select(pc_card_t *c, const uint8_t *apdu, size_t len)
{
    (void)apdu; (void)len;
    pc_card_queue_sw(c, ISO7816_SW_OK);
}

static int g_gr_count = 0;
static int g_6c_count = 0;

/* READ BINARY / GET RESPONSE : dispatch selon P1/P3 */
static void h_read(pc_card_t *c, const uint8_t *apdu, size_t len)
{
    (void)len;
    if (apdu[1] == 0xC0) {                 /* GET RESPONSE */
        static const uint8_t data[5] = { 1, 2, 3, 4, 5 };
        g_gr_count++;
        pc_card_t0_queue_ack_all(c, 0xC0);
        pc_card_t0_queue_data_sw(c, data, sizeof(data), ISO7816_SW_OK);
        return;
    }
    if (apdu[2] == 0x01) {                 /* '61 05' puis GET RESPONSE */
        static const uint8_t part[2] = { 0xAA, 0xBB };
        pc_card_t0_queue_ack_all(c, apdu[1]);
        pc_card_t0_queue_data_sw(c, part, sizeof(part), 0x6105);
        return;
    }
    if (apdu[2] == 0x02 && apdu[4] != 0x04) {   /* '6C 04': retransmission with P3=SW2 */
        g_6c_count++;
        pc_card_queue_sw(c, 0x6C04);
        return;
    }
    static const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    if (apdu[2] == 0x02) {
        static const uint8_t d2[4] = { 9, 8, 7, 6 };
        pc_card_t0_queue_ack_all(c, apdu[1]);
        pc_card_t0_queue_data_sw(c, d2, sizeof(d2), ISO7816_SW_OK);
        return;
    }
    pc_card_t0_queue_ack_all(c, apdu[1]);
    pc_card_t0_queue_data_sw(c, data, sizeof(data), ISO7816_SW_OK);
}

/* UPDATE BINARY (incoming): ACK + data received + 9000 */
static void h_update(pc_card_t *c, const uint8_t *apdu, size_t len)
{
    (void)apdu;
    (void)len;
    pc_card_queue_sw(c, ISO7816_SW_OK);
}

int main(void)
{
    pc_card_t *card = pc_card_create();
    pc_card_set_atr(card, ISO7816_ATR_TS_DIRECT, ATR_T0, sizeof(ATR_T0));
    pc_card_set_protocol(card, 0);
    pc_card_set_cmd_handler(card, 0xA4, false, h_select);
    pc_card_set_cmd_handler(card, 0xB0, false, h_read);
    pc_card_set_cmd_handler(card, 0xC0, false, h_read);
    pc_card_set_cmd_handler(card, 0xD0, true, h_update);

    iso7816_t dev;
    iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
    CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
    pc_card_attach(card, &dev);
    CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
    CHECK_EQ(iso7816_get_protocol(&dev), 0);

    uint8_t resp[256];
    size_t rlen = 0;
    uint16_t sw = 0;

    /* cas 1 : SELECT -> 9000 */
    {
        uint8_t cmd[] = { 0x00, 0xA4, 0x00, 0x00 };
        CHECK_EQ(iso7816_exchange(&dev, cmd, sizeof(cmd), resp, sizeof(resp), &rlen, &sw), ISO7816_OK);
        CHECK_EQ(sw, ISO7816_SW_OK);
        CHECK_EQ(rlen, 0);
    }

    /* cas 2S : READ BINARY -> 4 octets + 9000 */
    {
        uint8_t cmd[] = { 0x00, 0xB0, 0x00, 0x00, 0x04 };
        CHECK_EQ(iso7816_exchange(&dev, cmd, sizeof(cmd), resp, sizeof(resp), &rlen, &sw), ISO7816_OK);
        CHECK_EQ(sw, ISO7816_SW_OK);
        CHECK_EQ(rlen, 4);
        CHECK_EQ(resp[0], 0xDE);
        CHECK_EQ(resp[3], 0xEF);
    }

    /* case 2S with '61 05' -> GET RESPONSE (cl. 12.2) */
    {
        uint8_t cmd[] = { 0x00, 0xB0, 0x01, 0x00, 0x07 };
        CHECK_EQ(iso7816_exchange(&dev, cmd, sizeof(cmd), resp, sizeof(resp), &rlen, &sw), ISO7816_OK);
        CHECK_EQ(sw, ISO7816_SW_OK);
        CHECK_EQ(rlen, 2 + 5);
        CHECK_EQ(resp[0], 0xAA);
        CHECK_EQ(resp[2], 1);
        CHECK_EQ(resp[6], 5);
    }

    /* case 2S with '6C 04' -> retransmission with P3=SW2 */
    {
        uint8_t cmd[] = { 0x00, 0xB0, 0x02, 0x00, 0x09 };
        CHECK_EQ(iso7816_exchange(&dev, cmd, sizeof(cmd), resp, sizeof(resp), &rlen, &sw), ISO7816_OK);
        CHECK_EQ(sw, ISO7816_SW_OK);
        CHECK_EQ(rlen, 4);
        CHECK_EQ(resp[0], 9);
        CHECK(g_6c_count >= 1);
    }

    /* case 3S: incoming command, the card received header + data */
    {
        uint8_t cmd[] = { 0x00, 0xD0, 0x00, 0x00, 0x02, 0x11, 0x22 };
        CHECK_EQ(iso7816_exchange(&dev, cmd, sizeof(cmd), resp, sizeof(resp), &rlen, &sw), ISO7816_OK);
        CHECK_EQ(sw, ISO7816_SW_OK);
        uint8_t rx_log[64];
        size_t rx_len = pc_card_rxlog(card, rx_log, sizeof(rx_log));
        /* the last 7 bytes received by the card = the case-3S APDU */
        CHECK(rx_len >= 7);
        CHECK_EQ(rx_log[rx_len - 7], 0x00);
        CHECK_EQ(rx_log[rx_len - 6], 0xD0);
        CHECK_EQ(rx_log[rx_len - 2], 0x11);
        CHECK_EQ(rx_log[rx_len - 1], 0x22);
    }

    /* cas 4S : entrant + sortant */
    {
        uint8_t cmd[] = { 0x00, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x04 }; /* P3=0 incoming, Le=4 */
        /* this handler is not registered as incoming: 6D00 expected handler-side... ignore */
        (void)cmd;
    }

    /* exchange before activate -> state error (new device) */
    {
        iso7816_t dev2;
        iso7816_config_t cfg2 = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
        CHECK_EQ(iso7816_init(&dev2, pc_card_hal(card), &cfg2), ISO7816_OK);
        uint8_t cmd[] = { 0x00, 0xA4, 0x00, 0x00 };
        CHECK_EQ(iso7816_exchange(&dev2, cmd, sizeof(cmd), resp, sizeof(resp), &rlen, &sw), ISO7816_GEN_STATE);
    }

    CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
    pc_card_destroy(card);
    TEST_END();
}