/* hbi_test.h — a tiny, dependency-free unit-test harness for Hummingbird.
 *
 * Test-only support (not a module, never installed, never linked into the
 * library). Include it from a module's <mod>_test.c. It provides a minimal
 * check/run framework that prints a readable pass/fail line per case and makes
 * main() return non-zero if any case failed — exactly what CTest needs.
 *
 * Usage:
 *   #include "hbi_test.h"
 *   static void test_thing(void) {
 *       HBI_CHECK(1 + 1 == 2);
 *       HBI_CHECK_EQ_INT(answer(), 42);
 *   }
 *   int main(void) {
 *       HBI_TEST_BEGIN("mymodule");
 *       HBI_RUN(test_thing);
 *       return HBI_TEST_END();
 *   }
 *
 * The macros deliberately avoid any Hummingbird dependency so the harness can
 * test the lowest layers (common, platform) without a bootstrap-order problem.
 */
#ifndef HBI_TEST_H
#define HBI_TEST_H

#include <stdio.h>
#include <string.h>

/* Per-translation-unit counters. `static` is fine: each test binary is one TU
 * with its own main(), so there is no cross-file sharing to worry about. */
static int hbi_test__checks = 0;
static int hbi_test__failures = 0;
static const char *hbi_test__suite = "test";
static const char *hbi_test__current = "";

#define HBI_TEST_BEGIN(name)                                                                       \
    do {                                                                                           \
        hbi_test__suite = (name);                                                                  \
        printf("== %s ==\n", hbi_test__suite);                                                     \
    } while (0)

/* Run a `void(void)` test function, tagging any failures with its name. */
#define HBI_RUN(fn)                                                                                \
    do {                                                                                           \
        hbi_test__current = #fn;                                                                   \
        int before = hbi_test__failures;                                                           \
        fn();                                                                                      \
        if (hbi_test__failures == before) {                                                        \
            printf("  [ok]   %s\n", #fn);                                                          \
        }                                                                                          \
    } while (0)

/* Core assertion. Records a failure (does not abort) so one run reports every
 * broken case, not just the first. */
#define HBI_CHECK(cond)                                                                            \
    do {                                                                                           \
        ++hbi_test__checks;                                                                        \
        if (!(cond)) {                                                                             \
            ++hbi_test__failures;                                                                  \
            fprintf(stderr, "  [FAIL] %s: %s:%d: %s\n", hbi_test__current, __FILE__, __LINE__,     \
                    #cond);                                                                        \
        }                                                                                          \
    } while (0)

#define HBI_CHECK_MSG(cond, ...)                                                                   \
    do {                                                                                           \
        ++hbi_test__checks;                                                                        \
        if (!(cond)) {                                                                             \
            ++hbi_test__failures;                                                                  \
            fprintf(stderr, "  [FAIL] %s: %s:%d: ", hbi_test__current, __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
        }                                                                                          \
    } while (0)

#define HBI_CHECK_EQ_INT(a, b)                                                                     \
    do {                                                                                           \
        long long va = (long long)(a);                                                             \
        long long vb = (long long)(b);                                                             \
        ++hbi_test__checks;                                                                        \
        if (va != vb) {                                                                            \
            ++hbi_test__failures;                                                                  \
            fprintf(stderr, "  [FAIL] %s: %s:%d: %s (%lld) != %s (%lld)\n", hbi_test__current,     \
                    __FILE__, __LINE__, #a, va, #b, vb);                                           \
        }                                                                                          \
    } while (0)

#define HBI_CHECK_STR_EQ(a, b)                                                                     \
    do {                                                                                           \
        const char *sa = (a);                                                                      \
        const char *sb = (b);                                                                      \
        ++hbi_test__checks;                                                                        \
        if (sa == NULL || sb == NULL || strcmp(sa, sb) != 0) {                                     \
            ++hbi_test__failures;                                                                  \
            fprintf(stderr, "  [FAIL] %s: %s:%d: \"%s\" != \"%s\"\n", hbi_test__current, __FILE__, \
                    __LINE__, sa ? sa : "(null)", sb ? sb : "(null)");                             \
        }                                                                                          \
    } while (0)

/* Print the summary and yield a process exit code (0 = all passed). */
#define HBI_TEST_END()                                                                             \
    (printf("-- %s: %d checks run, %d passed, %d failed --\n", hbi_test__suite, hbi_test__checks,  \
            hbi_test__checks - hbi_test__failures, hbi_test__failures),                            \
     hbi_test__failures == 0 ? 0 : 1)

#endif /* HBI_TEST_H */
