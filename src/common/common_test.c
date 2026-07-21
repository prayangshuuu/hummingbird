/* common_test.c — unit tests for the `common` module: status table + error record.
 *
 * Self-contained (no test framework): a tiny CHECK macro that prints the first
 * failure and returns nonzero. Every module's test follows this same shape.
 */
#include "common/common.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

static void test_status_strings(void) {
    /* Every code below the sentinel maps to a non-NULL, non-empty string, and
     * none of them collides with the out-of-range fallback spelling. */
    for (int i = 0; i < HBI_STATUS_COUNT; ++i) {
        const char *s = hbi_status_str((hbi_status)i);
        CHECK(s != NULL);
        CHECK(s[0] != '\0');
        CHECK(strcmp(s, "HBI_ERR_UNKNOWN") != 0);
    }
    /* Out-of-range values fall back safely rather than indexing off the end. */
    CHECK(strcmp(hbi_status_str((hbi_status)HBI_STATUS_COUNT), "HBI_ERR_UNKNOWN") == 0);
    CHECK(strcmp(hbi_status_str((hbi_status)-1), "HBI_ERR_UNKNOWN") == 0);
}

static void test_error_record(void) {
    hbi_error_clear();
    const hbi_error *e = hbi_error_last();
    CHECK(e != NULL);
    CHECK(e->status == HBI_OK);
    CHECK(e->message[0] == '\0');
    CHECK(e->os_errno == 0);

    /* Setting an error records the status, os_errno, message, and a call-site. */
    hbi_status rc = HBI_ERR_SET(HBI_ERR_IO, 2, "open failed");
    CHECK(rc == HBI_ERR_IO);
    e = hbi_error_last();
    CHECK(e->status == HBI_ERR_IO);
    CHECK(e->os_errno == 2);
    CHECK(strcmp(e->message, "open failed") == 0);
    CHECK(e->line > 0);
    CHECK(e->file != NULL);
    CHECK(e->func != NULL && strcmp(e->func, "test_error_record") == 0);

    /* Formatting produces a single line that mentions the status and site. */
    char buf[512];
    int n = hbi_error_format(buf, sizeof buf);
    CHECK(n > 0);
    CHECK((size_t)n < sizeof buf);
    CHECK(strstr(buf, "HBI_ERR_IO") != NULL);
    CHECK(strstr(buf, "open failed") != NULL);
    CHECK(strstr(buf, "os_errno=2") != NULL);
    CHECK(strchr(buf, '\n') == NULL); /* single line */

    /* printf-style variant formats its arguments into the message. */
    rc = HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0, "no shard %d of %d", 3, 8);
    CHECK(rc == HBI_ERR_NOT_FOUND);
    e = hbi_error_last();
    CHECK(strcmp(e->message, "no shard 3 of 8") == 0);
    CHECK(e->os_errno == 0);

    /* Setting HBI_OK clears the record rather than recording a non-error. */
    rc = HBI_ERR_SET(HBI_OK, 0, "ignored");
    CHECK(rc == HBI_OK);
    e = hbi_error_last();
    CHECK(e->status == HBI_OK);
    CHECK(e->message[0] == '\0');
}

static void test_error_truncation(void) {
    /* An over-long message is truncated to the buffer and stays NUL-terminated. */
    char big[HBI_ERROR_MSG_CAP + 64];
    memset(big, 'x', sizeof big);
    big[sizeof big - 1] = '\0';
    hbi_error_set_at(HBI_ERR_INTERNAL, 0, __FILE__, __LINE__, __func__, big);
    const hbi_error *e = hbi_error_last();
    CHECK(strlen(e->message) == HBI_ERROR_MSG_CAP - 1);

    /* A tiny format buffer still NUL-terminates and reports the full length. */
    char tiny[8];
    int n = hbi_error_format(tiny, sizeof tiny);
    CHECK(tiny[sizeof tiny - 1] == '\0');
    CHECK(n >= 0);

    /* Zero-capacity format must not write and must not crash. */
    (void)hbi_error_format(NULL, 0);
    hbi_error_clear();
}

static void test_align_helpers(void) {
    CHECK(hbi_is_pow2(1));
    CHECK(hbi_is_pow2(4096));
    CHECK(!hbi_is_pow2(0));
    CHECK(!hbi_is_pow2(24));
    CHECK(hbi_align_up(0, 64) == 0);
    CHECK(hbi_align_up(1, 64) == 64);
    CHECK(hbi_align_up(64, 64) == 64);
    CHECK(hbi_align_up(100, 64) == 128);
    CHECK(hbi_align_up(10, 3) == 0); /* non-pow2 alignment => 0 */
}

int main(void) {
    CHECK(hbi_common_selftest() == HBI_OK);
    CHECK(strcmp(hbi_common_name(), "common") == 0);
    test_status_strings();
    test_error_record();
    test_error_truncation();
    test_align_helpers();

    if (g_failures != 0) {
        fprintf(stderr, "common: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("[ok] common: all checks passed\n");
    return 0;
}
