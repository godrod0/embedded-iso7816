#include "iso7816/iso7816_atr.h"

#include "test_main.h"

int main(void)
{
    iso7816_atr_t a;

    /* Minimal ATR: T0=0x00 -> only T=0 by default, no TCK */
    {
        uint8_t buf[] = { 0x00 };
        CHECK_EQ(iso7816_atr_parse(&a, ISO7816_ATR_TS_DIRECT, buf, sizeof(buf)), ISO7816_OK);
        CHECK(!a.inverse);
        CHECK_EQ(a.n_protocols, 0);
        CHECK_EQ(a.fi, ISO7816_F_DEFAULT);
        CHECK_EQ(a.di, 1);
        CHECK(!a.tck_present);
        CHECK(!a.has_t15);
    }

    /* convention inverse */
    {
        uint8_t buf[] = { 0x00 };
        CHECK_EQ(iso7816_atr_parse(&a, ISO7816_ATR_TS_INVERSE, buf, sizeof(buf)), ISO7816_OK);
        CHECK(a.inverse);
    }

    /* invalid TS */
    {
        uint8_t buf[] = { 0x00 };
        CHECK_EQ(iso7816_atr_parse(&a, 0x12, buf, sizeof(buf)), ISO7816_ATR_TS);
    }

    /* T=1 offert : T0=0x80, TD1=0x01, TCK=0x81 */
    {
        uint8_t buf[] = { 0x80, 0x01, 0x81 };
        CHECK_EQ(iso7816_atr_parse(&a, ISO7816_ATR_TS_DIRECT, buf, sizeof(buf)), ISO7816_OK);
        CHECK_EQ(a.n_protocols, 1);
        CHECK_EQ(a.protocols[0], 1);
        CHECK(a.has_t1);
        CHECK(a.tck_present);
        CHECK(a.tck_ok);
        CHECK_EQ(a.ifsc, 32);
        CHECK_EQ(a.cwi, 13);
        CHECK_EQ(a.bwi, 4);
        CHECK(!a.use_crc);
    }

    /* wrong TCK */
    {
        uint8_t buf[] = { 0x80, 0x01, 0x82 };
        CHECK_EQ(iso7816_atr_parse(&a, ISO7816_ATR_TS_DIRECT, buf, sizeof(buf)), ISO7816_ATR_TCK);
    }

    /* T=0 puis T=1 : T0=0x80, TD1=0x80 (Y2=8,T=0), TD2=0x01 (T=1), TCK=0x01 */
    {
        uint8_t buf[] = { 0x80, 0x80, 0x01, 0x01 };
        CHECK_EQ(iso7816_atr_parse(&a, ISO7816_ATR_TS_DIRECT, buf, sizeof(buf)), ISO7816_OK);
        CHECK_EQ(a.n_protocols, 2);
        CHECK_EQ(a.protocols[0], 0);
        CHECK_EQ(a.protocols[1], 1);
    }

    /* TA1/TB1/TC1: T0=0x70, TA1=0x11 (Fi=372/fmax=5, Di=1), TC1=0x00 */
    {
        uint8_t buf[] = { 0x70, 0x11, 0x00, 0x00 };
        CHECK_EQ(iso7816_atr_parse(&a, ISO7816_ATR_TS_DIRECT, buf, sizeof(buf)), ISO7816_OK);
        CHECK(a.has_ta1);
        CHECK_EQ(a.fi, ISO7816_F_DEFAULT);
        CHECK_EQ(a.di, 1);
        CHECK_EQ(a.fmax_mhz, 5);
        CHECK_EQ(a.guard_n, 0);
    }

    /* T=15: X=1 (clock stop L), Y=1 (class A only), SPU not used
       T0=0x80, TD1=0x3F (Y2=3,T=15), TA=0x41, TB=0x00, TCK=0xFE */
    {
        uint8_t buf[] = { 0x80, 0x3F, 0x41, 0x00, 0xFE };
        CHECK_EQ(iso7816_atr_parse(&a, ISO7816_ATR_TS_DIRECT, buf, sizeof(buf)), ISO7816_OK);
        CHECK(a.has_t15);
        CHECK_EQ(a.x_clock_stop, 1);
        CHECK_EQ(a.y_class, 1);
        CHECK(!a.spu_used);
        CHECK(a.tck_present && a.tck_ok);
    }

    /* T=1 with IFSC=8: T0=0x80, TD1=0x11, TA(T=1)=0x08, TCK=0x99 */
    {
        uint8_t buf[] = { 0x80, 0x11, 0x08, 0x99 };
        CHECK_EQ(iso7816_atr_parse(&a, ISO7816_ATR_TS_DIRECT, buf, sizeof(buf)), ISO7816_OK);
        CHECK(a.has_t1);
        CHECK_EQ(a.ifsc, 8);
    }

    /* inconsistent length -> malformed */
    {
        uint8_t buf[] = { 0x80, 0x01 };
        CHECK_EQ(iso7816_atr_parse(&a, ISO7816_ATR_TS_DIRECT, buf, sizeof(buf)), ISO7816_ATR_MALFORMED);
    }

    /* complétude incrémentale */
    {
        uint8_t buf[] = { 0x80, 0x01, 0x81 };
        CHECK(!iso7816_atr_complete(buf, 1));
        CHECK(!iso7816_atr_complete(buf, 2));
        CHECK(iso7816_atr_complete(buf, 3));
    }

    TEST_END();
}