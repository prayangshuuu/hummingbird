/* logging_test.c — unit tests for the logging module.
 *
 * Uses a capturing sink so the tests assert on exactly what the logger produced
 * without touching stderr. Covers: level ordering + strings, runtime level
 * filtering, the PROFILING-bypasses-filter rule, structured fields, message
 * formatting/truncation, and the enabled() predicate.
 */
#include "logging/logging.h"

#include "hbi_test.h"

#include <string.h>

/* ── A capturing sink: records the last event it received. ─────────────────── */
typedef struct capture {
    int count;
    hbi_log_level last_level;
    char last_message[512];
    int last_field_count;
    char last_first_value[128];
} capture;

static void capture_sink(void *ctx, const hbi_log_record *record) {
    capture *c = (capture *)ctx;
    c->count++;
    c->last_level = record->level;
    snprintf(c->last_message, sizeof(c->last_message), "%s", record->message);
    c->last_field_count = (int)record->field_count;
    if (record->field_count > 0 && record->fields[0].value != NULL) {
        snprintf(c->last_first_value, sizeof(c->last_first_value), "%s", record->fields[0].value);
    } else {
        c->last_first_value[0] = '\0';
    }
}

static capture g_cap;

static void reset_capture(void) {
    memset(&g_cap, 0, sizeof(g_cap));
}

static void test_level_strings(void) {
    HBI_CHECK_STR_EQ(hbi_log_level_str(HBI_LOG_TRACE), "trace");
    HBI_CHECK_STR_EQ(hbi_log_level_str(HBI_LOG_DEBUG), "debug");
    HBI_CHECK_STR_EQ(hbi_log_level_str(HBI_LOG_INFO), "info");
    HBI_CHECK_STR_EQ(hbi_log_level_str(HBI_LOG_WARN), "warn");
    HBI_CHECK_STR_EQ(hbi_log_level_str(HBI_LOG_ERROR), "error");
    HBI_CHECK_STR_EQ(hbi_log_level_str(HBI_LOG_PROFILING), "profiling");
    /* Out-of-range is safe. */
    HBI_CHECK(hbi_log_level_str((hbi_log_level)999) != NULL);
}

static void test_level_filter(void) {
    reset_capture();
    hbi_log_set_sink(capture_sink, &g_cap);
    hbi_log_set_level(HBI_LOG_WARN);

    HBI_CHECK(!hbi_log_enabled(HBI_LOG_INFO));
    HBI_CHECK(hbi_log_enabled(HBI_LOG_WARN));
    HBI_CHECK(hbi_log_enabled(HBI_LOG_ERROR));

    HB_LOG_INFO("this should be filtered");
    HBI_CHECK_EQ_INT(g_cap.count, 0);

    HB_LOG_ERROR("this should pass");
    HBI_CHECK_EQ_INT(g_cap.count, 1);
    HBI_CHECK_EQ_INT((int)g_cap.last_level, (int)HBI_LOG_ERROR);
    HBI_CHECK_STR_EQ(g_cap.last_message, "this should pass");

    hbi_log_set_level(HBI_LOG_INFO); /* restore default */
    hbi_log_set_sink(NULL, NULL);
}

static void test_profiling_bypasses_filter(void) {
    reset_capture();
    hbi_log_set_sink(capture_sink, &g_cap);
    hbi_log_set_level(HBI_LOG_ERROR); /* would filter everything below ERROR */

    HBI_CHECK(hbi_log_enabled(HBI_LOG_PROFILING));
    HB_LOG_PROF("timing=%d", 42);
    HBI_CHECK_EQ_INT(g_cap.count, 1);
    HBI_CHECK_EQ_INT((int)g_cap.last_level, (int)HBI_LOG_PROFILING);
    HBI_CHECK_STR_EQ(g_cap.last_message, "timing=42");

    hbi_log_set_level(HBI_LOG_INFO);
    hbi_log_set_sink(NULL, NULL);
}

static void test_message_formatting(void) {
    reset_capture();
    hbi_log_set_sink(capture_sink, &g_cap);
    hbi_log_set_level(HBI_LOG_TRACE);

    HB_LOG_INFO("value=%d name=%s", 7, "hb");
    HBI_CHECK_STR_EQ(g_cap.last_message, "value=7 name=hb");

    hbi_log_set_level(HBI_LOG_INFO);
    hbi_log_set_sink(NULL, NULL);
}

static void test_structured_fields(void) {
    reset_capture();
    hbi_log_set_sink(capture_sink, &g_cap);

    hbi_log_field fields[2] = {
        {"tier", "RAM"},
        {"bytes", "4096"},
    };
    HB_LOG_FIELDS(HBI_LOG_INFO, fields, 2, "placed expert");
    HBI_CHECK_EQ_INT(g_cap.count, 1);
    HBI_CHECK_EQ_INT(g_cap.last_field_count, 2);
    HBI_CHECK_STR_EQ(g_cap.last_first_value, "RAM");
    HBI_CHECK_STR_EQ(g_cap.last_message, "placed expert");

    hbi_log_set_sink(NULL, NULL);
}

static void test_selftest_and_identity(void) {
    HBI_CHECK_EQ_INT((int)hbi_logging_selftest(), (int)HBI_OK);
    HBI_CHECK_STR_EQ(hbi_logging_name(), "logging");
}

int main(void) {
    HBI_TEST_BEGIN("logging");
    HBI_RUN(test_level_strings);
    HBI_RUN(test_level_filter);
    HBI_RUN(test_profiling_bypasses_filter);
    HBI_RUN(test_message_formatting);
    HBI_RUN(test_structured_fields);
    HBI_RUN(test_selftest_and_identity);
    return HBI_TEST_END();
}
