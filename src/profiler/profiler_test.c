/* profiler_test.c — unit tests for the profiler module.
 *
 * Covers: enable/disable gating, scope timing aggregation, counters, events,
 * the report writer, overflow accounting, and reset. Timing assertions avoid
 * flakiness by checking structure (count, ordering, non-negative durations)
 * rather than exact nanosecond values.
 */
#include "profiler/profiler.h"

#include "hbi_test.h"

#include <stdio.h>
#include <string.h>

/* Find a stat by name in the current snapshot; returns false if absent. */
static bool find_stat(const char *name, hbi_prof_stat *out) {
    size_t n = hbi_prof_count();
    for (size_t i = 0; i < n; ++i) {
        hbi_prof_stat s;
        if (hbi_prof_stat_at(i, &s) != HBI_OK) {
            continue;
        }
        if (s.name != NULL && strcmp(s.name, name) == 0) {
            *out = s;
            return true;
        }
    }
    return false;
}

static void test_disabled_by_default(void) {
    hbi_prof_reset();
    HBI_CHECK(!hbi_prof_enabled());
    /* Recording while disabled is a no-op: no slots created. */
    hbi_prof_counter_add("nope", 5);
    hbi_prof_scope s = hbi_prof_scope_begin("nope_scope");
    hbi_prof_scope_end(&s);
    HBI_CHECK_EQ_INT((long long)hbi_prof_count(), 0);
}

static void test_counters(void) {
    hbi_prof_reset();
    hbi_prof_set_enabled(true);

    hbi_prof_counter_add("bytes", 100);
    hbi_prof_counter_add("bytes", 40);
    hbi_prof_counter_add("bytes", -10);

    hbi_prof_stat s;
    HBI_CHECK(find_stat("bytes", &s));
    HBI_CHECK_EQ_INT(s.kind, HBI_PROF_COUNTER);
    HBI_CHECK_EQ_INT(s.total, 130);
    HBI_CHECK_EQ_INT((long long)s.count, 3);

    hbi_prof_set_enabled(false);
}

static void test_scopes(void) {
    hbi_prof_reset();
    hbi_prof_set_enabled(true);

    for (int i = 0; i < 3; ++i) {
        hbi_prof_scope sc = hbi_prof_scope_begin("work");
        /* A little busy-work so the elapsed time is non-zero on most clocks. */
        volatile uint64_t acc = 0;
        for (int j = 0; j < 10000; ++j) {
            acc += (uint64_t)j;
        }
        (void)acc;
        hbi_prof_scope_end(&sc);
    }

    hbi_prof_stat s;
    HBI_CHECK(find_stat("work", &s));
    HBI_CHECK_EQ_INT(s.kind, HBI_PROF_SCOPE);
    HBI_CHECK_EQ_INT((long long)s.count, 3);
    HBI_CHECK(s.max_ns >= s.min_ns);
    HBI_CHECK(s.total_ns >= s.max_ns);

    hbi_prof_set_enabled(false);
}

static void test_events(void) {
    hbi_prof_reset();
    hbi_prof_set_enabled(true);

    hbi_prof_event("cache_miss", 0);
    hbi_prof_event("cache_miss", 0);

    hbi_prof_stat s;
    HBI_CHECK(find_stat("cache_miss", &s));
    HBI_CHECK_EQ_INT(s.kind, HBI_PROF_EVENT);
    HBI_CHECK_EQ_INT((long long)s.count, 2);

    hbi_prof_set_enabled(false);
}

static void test_report(void) {
    hbi_prof_reset();
    hbi_prof_set_enabled(true);
    hbi_prof_counter_add("alpha", 7);
    {
        hbi_prof_scope sc = hbi_prof_scope_begin("beta");
        hbi_prof_scope_end(&sc);
    }

    char buf[512];
    int need = hbi_prof_report(buf, sizeof(buf));
    HBI_CHECK(need > 0);
    HBI_CHECK(strstr(buf, "alpha") != NULL);
    HBI_CHECK(strstr(buf, "beta") != NULL);

    /* Truncation is detectable: a tiny buffer still NUL-terminates. */
    char tiny[8];
    int need2 = hbi_prof_report(tiny, sizeof(tiny));
    HBI_CHECK(need2 >= 0);
    HBI_CHECK(tiny[sizeof(tiny) - 1] == '\0' || strlen(tiny) < sizeof(tiny));

    hbi_prof_set_enabled(false);
}

static void test_overflow(void) {
    hbi_prof_reset();
    hbi_prof_set_enabled(true);
    HBI_CHECK_EQ_INT((long long)hbi_prof_overflow_count(), 0);

    /* Fill well past the table capacity with distinct names. */
    char name[32];
    for (int i = 0; i < HBI_PROF_MAX_NAMES + 50; ++i) {
        snprintf(name, sizeof(name), "n%d", i);
        hbi_prof_counter_add(name, 1);
    }
    HBI_CHECK(hbi_prof_count() <= (size_t)HBI_PROF_MAX_NAMES);
    HBI_CHECK(hbi_prof_overflow_count() > 0);

    hbi_prof_set_enabled(false);
    hbi_prof_reset();
}

static void test_identity(void) {
    HBI_CHECK_STR_EQ(hbi_profiler_name(), "profiler");
    HBI_CHECK_EQ_INT(hbi_profiler_selftest(), HBI_OK);
}

int main(void) {
    HBI_TEST_BEGIN("profiler");
    HBI_RUN(test_disabled_by_default);
    HBI_RUN(test_counters);
    HBI_RUN(test_scopes);
    HBI_RUN(test_events);
    HBI_RUN(test_report);
    HBI_RUN(test_overflow);
    HBI_RUN(test_identity);
    return HBI_TEST_END();
}
