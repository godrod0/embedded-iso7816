#include "iso7816/iso7816_apdu.h"

#include <string.h>

#include "test_main.h"

int main(void)
{
    iso7816_apdu_cmd_t decoded;
    uint8_t buf[300];

    /* cas 1 */
    {
        uint8_t in[] = { 0x00, 0xA4, 0x00, 0x00 };
        CHECK_EQ(iso7816_apdu_decode(in, sizeof(in), &decoded), ISO7816_OK);
        CHECK_EQ(decoded.apdu_case, ISO7816_APDU_CASE_1);
        CHECK_EQ(decoded.ins, 0xA4);
        CHECK_EQ(decoded.nc, 0);
        CHECK_EQ(decoded.ne, 0);
    }

    /* cas 2S, Ne=5 */
    {
        uint8_t in[] = { 0x00, 0xB0, 0x00, 0x00, 0x05 };
        CHECK_EQ(iso7816_apdu_decode(in, sizeof(in), &decoded), ISO7816_OK);
        CHECK_EQ(decoded.apdu_case, ISO7816_APDU_CASE_2S);
        CHECK_EQ(decoded.ne, 5);
    }

    /* cas 2S, Ne=256 ('00') */
    {
        uint8_t in[] = { 0x00, 0xB0, 0x00, 0x00, 0x00 };
        CHECK_EQ(iso7816_apdu_decode(in, sizeof(in), &decoded), ISO7816_OK);
        CHECK_EQ(decoded.apdu_case, ISO7816_APDU_CASE_2S);
        CHECK_EQ(decoded.ne, 256);
    }

    /* cas 3S */
    {
        uint8_t in[] = { 0x00, 0xD0, 0x00, 0x00, 0x02, 0xAA, 0xBB };
        CHECK_EQ(iso7816_apdu_decode(in, sizeof(in), &decoded), ISO7816_OK);
        CHECK_EQ(decoded.apdu_case, ISO7816_APDU_CASE_3S);
        CHECK_EQ(decoded.nc, 2);
        CHECK_EQ(decoded.data[0], 0xAA);
        CHECK_EQ(decoded.data[1], 0xBB);
        CHECK_EQ(decoded.ne, 0);
    }

    /* cas 4S */
    {
        uint8_t in[] = { 0x00, 0xB0, 0x00, 0x00, 0x02, 0xAA, 0xBB, 0x05 };
        CHECK_EQ(iso7816_apdu_decode(in, sizeof(in), &decoded), ISO7816_OK);
        CHECK_EQ(decoded.apdu_case, ISO7816_APDU_CASE_4S);
        CHECK_EQ(decoded.nc, 2);
        CHECK_EQ(decoded.ne, 5);
    }

    /* cas 2E : Ne = 0x0100 = 256 */
    {
        uint8_t in[] = { 0x00, 0xB0, 0x00, 0x00, 0x00, 0x01, 0x00 };
        CHECK_EQ(iso7816_apdu_decode(in, sizeof(in), &decoded), ISO7816_OK);
        CHECK_EQ(decoded.apdu_case, ISO7816_APDU_CASE_2E);
        CHECK_EQ(decoded.ne, 256);
    }

    /* cas 3E */
    {
        uint8_t in[] = { 0x00, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x03, 0xAA, 0xBB, 0xCC };
        CHECK_EQ(iso7816_apdu_decode(in, sizeof(in), &decoded), ISO7816_OK);
        CHECK_EQ(decoded.apdu_case, ISO7816_APDU_CASE_3E);
        CHECK_EQ(decoded.nc, 3);
    }

    /* cas 4E */
    {
        uint8_t in[] = { 0x00, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x02, 0xAA, 0xBB, 0x00, 0x10 };
        CHECK_EQ(iso7816_apdu_decode(in, sizeof(in), &decoded), ISO7816_OK);
        CHECK_EQ(decoded.apdu_case, ISO7816_APDU_CASE_4E);
        CHECK_EQ(decoded.nc, 2);
        CHECK_EQ(decoded.ne, 16);
    }

    /* invalide : longueur incohérente */
    {
        uint8_t in[] = { 0x00, 0xB0, 0x00, 0x00, 0x05, 0xAA };
        CHECK_EQ(iso7816_apdu_decode(in, sizeof(in), &decoded), ISO7816_APDU_INVALID);
    }

    /* encodage : round-trip cas 4S */
    {
        uint8_t data[2] = { 0xAA, 0xBB };
        iso7816_apdu_cmd_t apdu_cmd = { 0x00, 0xB0, 0x00, 0x00, ISO7816_APDU_CASE_4S, 2, data, 5 };
        size_t out_len = iso7816_apdu_encode(&apdu_cmd, buf, sizeof(buf));
        CHECK_EQ(out_len, 8);
        uint8_t exp[] = { 0x00, 0xB0, 0x00, 0x00, 0x02, 0xAA, 0xBB, 0x05 };
        CHECK(memcmp(buf, exp, 8) == 0);
        CHECK_EQ(iso7816_apdu_decode(buf, out_len, &decoded), ISO7816_OK);
        CHECK_EQ(decoded.apdu_case, ISO7816_APDU_CASE_4S);
        CHECK_EQ(decoded.nc, 2);
        CHECK_EQ(decoded.ne, 5);
    }

    /* encodage cas 2E, Ne=65536 -> '0000' */
    {
        iso7816_apdu_cmd_t apdu_cmd = { 0x00, 0xB0, 0x00, 0x00, ISO7816_APDU_CASE_2E, 0, NULL, 65536 };
        size_t out_len = iso7816_apdu_encode(&apdu_cmd, buf, sizeof(buf));
        CHECK_EQ(out_len, 7);
        CHECK_EQ(buf[4], 0x00);
        CHECK_EQ(buf[5], 0x00);
        CHECK_EQ(buf[6], 0x00);
    }

    TEST_END();
}