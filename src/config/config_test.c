/* config_test.c — real coverage for the typed configuration module.
 *
 * Exercises the schema, defaults, typed get/set with bounds validation, the
 * "key=value" file parser, precedence (default < file < programmatic set), and
 * introspection. Environment loading is covered indirectly (load_env with no
 * matching vars is a no-op that must not disturb existing values) to avoid the
 * platform-specific setenv/_putenv split in a unit test.
 */
#include "config/config.h"
#include "hbi_test.h"

#include <stdio.h>

/* A small but representative schema, terminated by a NULL-key sentinel. */
static const hbi_config_desc k_schema[] = {
    {"log.level", HBI_CFG_STRING, "HB_LOG_LEVEL", "minimum log level", false, 0, 0, "info", 0, 0, 0,
     0},
    {"runtime.threads", HBI_CFG_UINT, "HB_THREADS", "worker thread count", false, 0, 4, NULL, 0, 0,
     1, 256},
    {"runtime.verbose", HBI_CFG_BOOL, "HB_VERBOSE", "verbose startup", false, 0, 0, NULL, 0, 0, 0,
     0},
    {"sched.bias", HBI_CFG_INT, NULL, "scheduler bias", false, 0, 0, NULL, -10, 10, 0, 0},
    {NULL, HBI_CFG_BOOL, NULL, NULL, false, 0, 0, NULL, 0, 0, 0, 0}};

static void test_defaults(void) {
    hbi_config *cfg = NULL;
    HBI_CHECK_EQ_INT(hbi_config_create(&cfg, k_schema), HBI_OK);
    HBI_CHECK(cfg != NULL);
    HBI_CHECK_STR_EQ(hbi_config_get_string(cfg, "log.level", "?"), "info");
    HBI_CHECK_EQ_INT((long long)hbi_config_get_uint(cfg, "runtime.threads", 0), 4);
    HBI_CHECK(hbi_config_get_bool(cfg, "runtime.verbose", true) == false);
    HBI_CHECK_EQ_INT(hbi_config_get_int(cfg, "sched.bias", 99), 0);
    HBI_CHECK_EQ_INT(hbi_config_count(cfg), 4);
    hbi_config_destroy(cfg);
}

static void test_typed_set_and_bounds(void) {
    hbi_config *cfg = NULL;
    HBI_CHECK_EQ_INT(hbi_config_create(&cfg, k_schema), HBI_OK);

    HBI_CHECK_EQ_INT(hbi_config_set_uint(cfg, "runtime.threads", 16), HBI_OK);
    HBI_CHECK_EQ_INT((long long)hbi_config_get_uint(cfg, "runtime.threads", 0), 16);

    /* Out-of-bounds is rejected and the old value survives. */
    HBI_CHECK_EQ_INT(hbi_config_set_uint(cfg, "runtime.threads", 999), HBI_ERR_CORRUPT);
    HBI_CHECK_EQ_INT((long long)hbi_config_get_uint(cfg, "runtime.threads", 0), 16);

    HBI_CHECK_EQ_INT(hbi_config_set_int(cfg, "sched.bias", -5), HBI_OK);
    HBI_CHECK_EQ_INT(hbi_config_get_int(cfg, "sched.bias", 0), -5);
    HBI_CHECK_EQ_INT(hbi_config_set_int(cfg, "sched.bias", 11), HBI_ERR_CORRUPT);

    /* Type mismatch and unknown key are distinct, reported errors. */
    HBI_CHECK_EQ_INT(hbi_config_set_bool(cfg, "runtime.threads", true), HBI_ERR_INVALID_ARG);
    HBI_CHECK_EQ_INT(hbi_config_set_int(cfg, "no.such.key", 1), HBI_ERR_NOT_FOUND);

    /* A programmatic set records the SET source. */
    HBI_CHECK_EQ_INT(hbi_config_source_of(cfg, "runtime.threads"), HBI_CFG_SRC_SET);
    hbi_config_destroy(cfg);
}

static void test_file_load_and_precedence(void) {
    /* Write a temp config file with stdlib I/O (warning-clean, portable). */
    const char *path = "hb_config_test.tmp";
    FILE *f = fopen(path, "wb");
    HBI_CHECK(f != NULL);
    if (f) {
        fputs("# a comment line\n", f);
        fputs("\n", f);                  /* blank line ignored */
        fputs("log.level = debug\n", f); /* whitespace trimmed */
        fputs("runtime.threads=8\n", f);
        fputs("runtime.verbose = true\n", f);
        fclose(f);
    }

    hbi_config *cfg = NULL;
    HBI_CHECK_EQ_INT(hbi_config_create(&cfg, k_schema), HBI_OK);
    HBI_CHECK_EQ_INT(hbi_config_load_file(cfg, path), HBI_OK);

    HBI_CHECK_STR_EQ(hbi_config_get_string(cfg, "log.level", "?"), "debug");
    HBI_CHECK_EQ_INT((long long)hbi_config_get_uint(cfg, "runtime.threads", 0), 8);
    HBI_CHECK(hbi_config_get_bool(cfg, "runtime.verbose", false) == true);
    HBI_CHECK_EQ_INT(hbi_config_source_of(cfg, "log.level"), HBI_CFG_SRC_FILE);

    /* env load with no matching vars must not disturb file-set values. */
    HBI_CHECK_EQ_INT(hbi_config_load_env(cfg), HBI_OK);
    HBI_CHECK_EQ_INT((long long)hbi_config_get_uint(cfg, "runtime.threads", 0), 8);

    /* A programmatic set (higher precedence) overrides the file value. */
    HBI_CHECK_EQ_INT(hbi_config_set_uint(cfg, "runtime.threads", 32), HBI_OK);
    HBI_CHECK_EQ_INT((long long)hbi_config_get_uint(cfg, "runtime.threads", 0), 32);

    hbi_config_destroy(cfg);
    remove(path);
}

static void test_apply_kv_and_errors(void) {
    hbi_config *cfg = NULL;
    HBI_CHECK_EQ_INT(hbi_config_create(&cfg, k_schema), HBI_OK);

    HBI_CHECK_EQ_INT(hbi_config_apply_kv(cfg, "sched.bias", "7"), HBI_OK);
    HBI_CHECK_EQ_INT(hbi_config_get_int(cfg, "sched.bias", 0), 7);

    /* Unknown key and un-parseable value are reported, not silently ignored. */
    HBI_CHECK_EQ_INT(hbi_config_apply_kv(cfg, "bogus", "1"), HBI_ERR_NOT_FOUND);
    HBI_CHECK_EQ_INT(hbi_config_apply_kv(cfg, "sched.bias", "notanumber"), HBI_ERR_CORRUPT);
    HBI_CHECK_EQ_INT(hbi_config_apply_kv(cfg, "runtime.verbose", "yes"), HBI_OK);
    HBI_CHECK(hbi_config_get_bool(cfg, "runtime.verbose", false) == true);

    hbi_config_destroy(cfg);
}

static void test_missing_file_and_bad_schema(void) {
    hbi_config *cfg = NULL;
    HBI_CHECK_EQ_INT(hbi_config_create(&cfg, k_schema), HBI_OK);
    /* A missing file reports NOT_FOUND (propagated from the file layer), not a crash. */
    HBI_CHECK_EQ_INT(hbi_config_load_file(cfg, "definitely-not-here.tmp"), HBI_ERR_NOT_FOUND);
    hbi_config_destroy(cfg);

    /* NULL schema is rejected. */
    HBI_CHECK_EQ_INT(hbi_config_create(&cfg, NULL), HBI_ERR_INVALID_ARG);
}

static void test_introspection(void) {
    hbi_config *cfg = NULL;
    HBI_CHECK_EQ_INT(hbi_config_create(&cfg, k_schema), HBI_OK);
    size_t n = hbi_config_count(cfg);
    HBI_CHECK_EQ_INT(n, 4);
    for (size_t i = 0; i < n; ++i) {
        const hbi_config_desc *d = hbi_config_desc_at(cfg, i);
        HBI_CHECK(d != NULL && d->key != NULL && d->help != NULL);
        HBI_CHECK(hbi_config_type_str(d->type) != NULL);
    }
    HBI_CHECK(hbi_config_desc_at(cfg, n) == NULL); /* out of range */
    hbi_config_destroy(cfg);
}

static void test_identity(void) {
    HBI_CHECK_EQ_INT(hbi_config_selftest(), HBI_OK);
    HBI_CHECK_STR_EQ(hbi_config_name(), "config");
}

int main(void) {
    HBI_TEST_BEGIN("config");
    HBI_RUN(test_defaults);
    HBI_RUN(test_typed_set_and_bounds);
    HBI_RUN(test_file_load_and_precedence);
    HBI_RUN(test_apply_kv_and_errors);
    HBI_RUN(test_missing_file_and_bad_schema);
    HBI_RUN(test_introspection);
    HBI_RUN(test_identity);
    return HBI_TEST_END();
}
