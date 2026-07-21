/* core_test.c — unit tests for the Runtime Context.
 *
 * Covers: default config, full lifecycle (create/ready/destroy), state machine
 * transitions, accessors, the device readback, and the subsystem registry with
 * reverse-order finalization. No inference logic — this is the lifecycle layer.
 */
#include "core/core.h"

#include "hbi_test.h"

#include <string.h>

static void test_state_strings(void) {
    HBI_CHECK_STR_EQ(hbi_core_state_str(HBI_CORE_UNINIT), "uninit");
    HBI_CHECK_STR_EQ(hbi_core_state_str(HBI_CORE_READY), "ready");
    HBI_CHECK_STR_EQ(hbi_core_state_str(HBI_CORE_DEAD), "dead");
}

static void test_config_default(void) {
    hbi_core_config cfg;
    memset(&cfg, 0xAB, sizeof(cfg));
    HBI_CHECK_EQ_INT(hbi_core_config_default(&cfg), HBI_OK);
    HBI_CHECK_EQ_INT(cfg.log_level, HBI_LOG_INFO);
    HBI_CHECK_EQ_INT(cfg.num_workers, 0);
    HBI_CHECK(cfg.allocator == NULL);
    HBI_CHECK(!cfg.enable_profiling);
    HBI_CHECK_EQ_INT(hbi_core_config_default(NULL), HBI_ERR_INVALID_ARG);
}

static void test_lifecycle_defaults(void) {
    hbi_core *core = NULL;
    HBI_CHECK_EQ_INT(hbi_core_create(&core, NULL), HBI_OK);
    HBI_CHECK(core != NULL);
    HBI_CHECK_EQ_INT(hbi_core_get_state(core), HBI_CORE_READY);

    /* Accessors return live subsystems when READY. */
    HBI_CHECK(hbi_core_allocator(core) != NULL);
    HBI_CHECK(hbi_core_threadpool(core) != NULL);
    HBI_CHECK(hbi_core_config_get(core) != NULL);

    /* The pool was sized to at least one worker. */
    HBI_CHECK(hbi_threadpool_worker_count(hbi_core_threadpool(core)) >= 1);

    /* Device report is available and sane. */
    hbi_device_info dev;
    HBI_CHECK_EQ_INT(hbi_core_device(core, &dev), HBI_OK);
    HBI_CHECK(dev.logical_cores >= 1);

    hbi_core_destroy(core);
}

static void test_lifecycle_explicit_config(void) {
    hbi_core_config cfg;
    hbi_core_config_default(&cfg);
    cfg.num_workers = 2;
    cfg.queue_capacity = 16;

    hbi_core *core = NULL;
    HBI_CHECK_EQ_INT(hbi_core_create(&core, &cfg), HBI_OK);
    HBI_CHECK(core != NULL);
    HBI_CHECK_EQ_INT(hbi_threadpool_worker_count(hbi_core_threadpool(core)), 2);

    /* The effective config records what was applied. */
    const hbi_config *ecfg = hbi_core_config_get(core);
    HBI_CHECK(ecfg != NULL);
    HBI_CHECK_EQ_INT(hbi_config_get_int(ecfg, "core.workers", -1), 2);
    HBI_CHECK_EQ_INT(hbi_config_get_uint(ecfg, "core.queue_capacity", 0), 16);

    hbi_core_destroy(core);
}

static void test_accessors_reject_non_ready(void) {
    /* NULL context: accessors are NULL, state is UNINIT, no crash. */
    HBI_CHECK(hbi_core_allocator(NULL) == NULL);
    HBI_CHECK(hbi_core_threadpool(NULL) == NULL);
    HBI_CHECK(hbi_core_config_get(NULL) == NULL);
    HBI_CHECK_EQ_INT(hbi_core_get_state(NULL), HBI_CORE_UNINIT);

    hbi_device_info dev;
    HBI_CHECK_EQ_INT(hbi_core_device(NULL, &dev), HBI_ERR_STATE);
}

/* Subsystem registry: a fake subsystem whose finalizer bumps a counter, used to
 * prove reverse-order teardown. */
static int g_fini_order[4];
static int g_fini_count;
static int g_tag_a = 1, g_tag_b = 2, g_tag_c = 3;

static void record_fini(void *ptr) {
    g_fini_order[g_fini_count++] = *(int *)ptr;
}

static void test_subsystem_registry(void) {
    g_fini_count = 0;
    hbi_core *core = NULL;
    HBI_CHECK_EQ_INT(hbi_core_create(&core, NULL), HBI_OK);

    HBI_CHECK_EQ_INT(hbi_core_register(core, "a", &g_tag_a, record_fini), HBI_OK);
    HBI_CHECK_EQ_INT(hbi_core_register(core, "b", &g_tag_b, record_fini), HBI_OK);
    HBI_CHECK_EQ_INT(hbi_core_register(core, "c", &g_tag_c, record_fini), HBI_OK);
    HBI_CHECK_EQ_INT((long long)hbi_core_subsystem_count(core), 3);

    /* Lookup returns the registered pointer; unknown returns NULL. */
    HBI_CHECK(hbi_core_lookup(core, "b") == &g_tag_b);
    HBI_CHECK(hbi_core_lookup(core, "missing") == NULL);

    /* Duplicate name is rejected. */
    HBI_CHECK_EQ_INT(hbi_core_register(core, "a", &g_tag_a, NULL), HBI_ERR_CORRUPT);

    /* Bad args. */
    HBI_CHECK_EQ_INT(hbi_core_register(core, NULL, &g_tag_a, NULL), HBI_ERR_INVALID_ARG);
    HBI_CHECK_EQ_INT(hbi_core_register(core, "x", NULL, NULL), HBI_ERR_INVALID_ARG);

    hbi_core_destroy(core);

    /* Finalizers ran in reverse registration order: c, b, a. */
    HBI_CHECK_EQ_INT(g_fini_count, 3);
    HBI_CHECK_EQ_INT(g_fini_order[0], 3);
    HBI_CHECK_EQ_INT(g_fini_order[1], 2);
    HBI_CHECK_EQ_INT(g_fini_order[2], 1);
}

static void test_identity(void) {
    HBI_CHECK_EQ_INT(hbi_core_selftest(), HBI_OK);
    HBI_CHECK_STR_EQ(hbi_core_name(), "core");
}

int main(void) {
    HBI_TEST_BEGIN("core");
    HBI_RUN(test_state_strings);
    HBI_RUN(test_config_default);
    HBI_RUN(test_lifecycle_defaults);
    HBI_RUN(test_lifecycle_explicit_config);
    HBI_RUN(test_accessors_reject_non_ready);
    HBI_RUN(test_subsystem_registry);
    HBI_RUN(test_identity);
    return HBI_TEST_END();
}
