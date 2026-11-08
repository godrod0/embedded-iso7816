#include "iso7816/iso7816.h"

#include "pc_card.h"
#include "test_main.h"

static const uint8_t ATR_T0[] = { 0x00 };

/* T=15, Y = class B only: T0=0x80, TD1=0x3F, TA=0x42, TB=0x00, TCK=0xFD */
static const uint8_t ATR_CLASS_B[] = { 0x80, 0x3F, 0x42, 0x00, 0xFD };

/* T=15, X=3 (clock stop no preference), Y = A only: T0=0x80, TD1=0x3F, TA=0xC1, TB=0x00, TCK=0x7E */
static const uint8_t ATR_CLKSTOP[] = { 0x80, 0x3F, 0xC1, 0x00, 0x7E };

/* T=15, X=0 (no clock stop), Y = A only: T0=0x80, TD1=0x3F, TA=0x01, TB=0x00, TCK=0xBE */
static const uint8_t ATR_NOCLKSTOP[] = { 0x80, 0x3F, 0x01, 0x00, 0xBE };

static void h_ok(pc_card_t *c, const uint8_t *apdu, size_t len)
{
    (void)apdu; (void)len;
    pc_card_queue_sw(c, ISO7816_SW_OK);
}

int main(void)
{
    /* --- silent card: no answer to reset --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_silent(card, true);
        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_DEV_NO_ANSWER);
        CHECK(!dev.active);
        pc_card_destroy(card);
    }

    /* --- class selection: A requested, the card only accepts B --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_atr(card, ISO7816_ATR_TS_DIRECT, ATR_CLASS_B, sizeof(ATR_CLASS_B));
        pc_card_set_protocol(card, 0);
        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
        CHECK_EQ(dev.cls, ISO7816_CLASS_B);          /* fallback to class B (cl. 6.2.4) */
        CHECK_EQ(dev.atr.y_class, 0x02);
        CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
        pc_card_destroy(card);
    }

    /* --- warm reset --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_atr(card, ISO7816_ATR_TS_DIRECT, ATR_T0, sizeof(ATR_T0));
        pc_card_set_protocol(card, 0);
        pc_card_set_cmd_handler(card, 0xA4, false, h_ok);
        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
        CHECK_EQ(iso7816_warm_reset(&dev), ISO7816_OK);
        CHECK(dev.active);
        uint8_t resp[16];
        size_t rlen;
        uint16_t sw;
        uint8_t cmd[] = { 0x00, 0xA4, 0x00, 0x00 };
        CHECK_EQ(iso7816_exchange(&dev, cmd, sizeof(cmd), resp, sizeof(resp), &rlen, &sw), ISO7816_OK);
        CHECK_EQ(sw, ISO7816_SW_OK);
        CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
        pc_card_destroy(card);
    }

    /* --- clock stop supported (X=3) --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_atr(card, ISO7816_ATR_TS_DIRECT, ATR_CLKSTOP, sizeof(ATR_CLKSTOP));
        pc_card_set_protocol(card, 0);
        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
        CHECK_EQ(dev.atr.x_clock_stop, 3);
        CHECK_EQ(iso7816_clock_stop(&dev), ISO7816_OK);
        CHECK(dev.clock_stopped);
        CHECK_EQ(iso7816_clock_restart(&dev), ISO7816_OK);
        CHECK(!dev.clock_stopped);
        CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
        pc_card_destroy(card);
    }

    /* --- clock stop not supported (X=0) --- */
    {
        pc_card_t *card = pc_card_create();
        pc_card_set_atr(card, ISO7816_ATR_TS_DIRECT, ATR_NOCLKSTOP, sizeof(ATR_NOCLKSTOP));
        pc_card_set_protocol(card, 0);
        iso7816_t dev;
        iso7816_config_t cfg = { .hal = pc_card_hal(card), .clk_hz = ISO7816_CLK_RESET_MIN_HZ };
        CHECK_EQ(iso7816_init(&dev, pc_card_hal(card), &cfg), ISO7816_OK);
        pc_card_attach(card, &dev);
        CHECK_EQ(iso7816_activate(&dev, ISO7816_CLASS_A), ISO7816_OK);
        CHECK_EQ(iso7816_clock_stop(&dev), ISO7816_GEN_STATE);
        CHECK_EQ(iso7816_deactivate(&dev), ISO7816_OK);
        pc_card_destroy(card);
    }

    TEST_END();
}