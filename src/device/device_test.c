/* device_test.c — unit tests for the `device` capability report. */
#include "device/device.h"

#include "hbi_test.h"

#include <stdio.h>
#include <string.h>

static void test_simd_level_strings(void) {
    for (int i = 0; i < HBI_SIMD_LEVEL_COUNT; ++i) {
        const char *s = hbi_simd_level_str((hbi_simd_level)i);
        HBI_CHECK(s != NULL && s[0] != '\0');
    }
    /* An out-of-range value must not crash and must not return NULL. */
    HBI_CHECK(hbi_simd_level_str((hbi_simd_level)999) != NULL);
    /* The compiled level is one of the known values. */
    hbi_simd_level lvl = hbi_device_simd_level();
    HBI_CHECK(lvl >= HBI_SIMD_NONE && lvl < HBI_SIMD_LEVEL_COUNT);
}

static void test_query(void) {
    HBI_CHECK_EQ_INT(hbi_device_query(NULL), HBI_ERR_INVALID_ARG);

    hbi_device_info info;
    memset(&info, 0, sizeof info);
    HBI_CHECK_EQ_INT(hbi_device_query(&info), HBI_OK);

    /* Core counts are sane and self-consistent. */
    HBI_CHECK(info.logical_cores >= 1);
    HBI_CHECK(info.physical_cores >= 1);
    HBI_CHECK(info.physical_cores <= info.logical_cores);
    HBI_CHECK(info.page_size >= 512);
    HBI_CHECK(info.cacheline_size >= 16);
    HBI_CHECK(info.arch[0] != '\0');
    HBI_CHECK(info.simd >= HBI_SIMD_NONE && info.simd < HBI_SIMD_LEVEL_COUNT);

    /* The report is stable across calls. */
    hbi_device_info again;
    memset(&again, 0, sizeof again);
    HBI_CHECK_EQ_INT(hbi_device_query(&again), HBI_OK);
    HBI_CHECK_EQ_INT(again.logical_cores, info.logical_cores);
    HBI_CHECK_EQ_INT(again.simd, info.simd);
}

static void test_accessors(void) {
    /* logical_cores is clamped to >= 1 so callers can divide by it. */
    HBI_CHECK(hbi_device_logical_cores() >= 1);
}

static void test_describe(void) {
    char buf[128];
    int n = hbi_device_describe(buf, sizeof buf);
    HBI_CHECK(n > 0);
    HBI_CHECK(strlen(buf) > 0);
    HBI_CHECK(strstr(buf, "simd=") != NULL);

    /* Truncation is reported (return value is the full length like snprintf). */
    char small[8];
    int n2 = hbi_device_describe(small, sizeof small);
    HBI_CHECK(n2 == n); /* would-be length, not the truncated length */
    HBI_CHECK(strlen(small) < sizeof small);

    /* cap == 0 must not write and must not crash. */
    int n3 = hbi_device_describe(NULL, 0);
    HBI_CHECK(n3 == n);
}

static void test_identity(void) {
    HBI_CHECK_EQ_INT(hbi_device_selftest(), HBI_OK);
    HBI_CHECK_STR_EQ(hbi_device_name(), "device");
}

int main(void) {
    HBI_TEST_BEGIN("device");
    HBI_RUN(test_simd_level_strings);
    HBI_RUN(test_query);
    HBI_RUN(test_accessors);
    HBI_RUN(test_describe);
    HBI_RUN(test_identity);
    return HBI_TEST_END();
}
