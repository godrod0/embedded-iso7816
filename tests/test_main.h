#pragma once

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                                        \
    do {                                                                                      \
        long _a = (long)(a), _b = (long)(b);                                                  \
        if (_a != _b) {                                                                       \
            printf("FAIL %s:%d: %s (=%ld) != %s (=%ld)\n", __FILE__, __LINE__, #a, _a, #b, _b); \
            g_failures++;                                                                     \
        }                                                                                     \
    } while (0)

#define TEST_END()                          \
    do {                                    \
        if (g_failures) {                   \
            printf("%d echec(s)\n", g_failures); \
            return 1;                       \
        }                                   \
        printf("OK\n");                     \
        return 0;                           \
    } while (0)