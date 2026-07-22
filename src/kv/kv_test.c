/* kv_test.c — Comprehensive unit tests for the KV Cache Manager (RFC-012). */
#include "kv/kv.h"
#include "kv/kv_internal.h"
#include "memory/memory.h"
#include "tensor/tensor.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int test_manager_lifecycle(void) {
    hbi_kv_manager *mgr = NULL;
    hbi_status st = hbi_kv_manager_create(hbi_allocator_system(), NULL, &mgr);
    ASSERT(st == HBI_OK, "manager create");
    ASSERT(mgr != NULL, "manager ptr is valid");

    hbi_kv_statistics stats;
    st = hbi_kv_manager_get_statistics(mgr, &stats);
    ASSERT(st == HBI_OK, "manager get stats");
    ASSERT(stats.active_contexts == 0, "no active contexts");
    ASSERT(stats.total_allocations == 0, "no allocations");

    hbi_kv_manager_destroy(mgr);
    return 0;
}

static int test_context_lifecycle_and_append(void) {
    hbi_kv_manager *mgr = NULL;
    hbi_kv_manager_create(hbi_allocator_system(), NULL, &mgr);

    hbi_context_handle *ctx = NULL;
    hbi_shape shape;
    int64_t dims[4] = {3, 32, 1024, 64};
    hbi_shape_init(&shape, dims, 4);
    hbi_status st = hbi_kv_context_create(mgr, 1024, HBI_DTYPE_FP16, &shape, &shape, &ctx);
    ASSERT(st == HBI_OK, "context create");
    ASSERT(ctx != NULL, "context handle is valid");

    hbi_kv_statistics stats;
    hbi_kv_manager_get_statistics(mgr, &stats);
    ASSERT(stats.active_contexts == 1, "active_contexts == 1");

    hbi_context_state state;
    st = hbi_kv_context_get_state(mgr, ctx, &state);
    ASSERT(st == HBI_OK, "get state");
    ASSERT(state.total_tokens == 0, "initially 0 tokens");
    ASSERT(state.max_tokens == 1024, "max tokens is 1024");
    ASSERT(state.num_pages == 1, "contiguous allocator uses 1 page");

    const hbi_kv_page *page = NULL;
    st = hbi_kv_context_get_page(mgr, ctx, 0, &page);
    ASSERT(st == HBI_OK, "get page 0");
    ASSERT(page->capacity == 1024, "page capacity is 1024");
    ASSERT(page->num_tokens == 0, "page num_tokens is 0");

    /* Append tokens */
    st = hbi_kv_context_append_tokens(mgr, ctx, 128);
    ASSERT(st == HBI_OK, "append 128 tokens");

    hbi_kv_context_get_state(mgr, ctx, &state);
    ASSERT(state.total_tokens == 128, "total_tokens == 128");

    /* Exceed capacity */
    st = hbi_kv_context_append_tokens(mgr, ctx, 1000);
    ASSERT(st == HBI_ERR_INVALID_ARG, "exceeding capacity rejected");

    /* Reset */
    st = hbi_kv_context_reset(mgr, ctx);
    ASSERT(st == HBI_OK, "reset");

    hbi_kv_context_get_state(mgr, ctx, &state);
    ASSERT(state.total_tokens == 0, "total_tokens reset to 0");

    hbi_kv_context_destroy(mgr, ctx);

    hbi_kv_manager_get_statistics(mgr, &stats);
    ASSERT(stats.active_contexts == 0, "active_contexts == 0 after destroy");
    ASSERT(stats.current_memory_bytes == 0, "memory tracked correctly");

    hbi_kv_manager_destroy(mgr);
    return 0;
}

static int test_context_truncate(void) {
    hbi_kv_manager *mgr = NULL;
    hbi_kv_manager_create(hbi_allocator_system(), NULL, &mgr);

    hbi_context_handle *ctx = NULL;
    hbi_shape shape;
    int64_t dims[4] = {3, 32, 1024, 64};
    hbi_shape_init(&shape, dims, 4);
    hbi_kv_context_create(mgr, 1024, HBI_DTYPE_FP16, &shape, &shape, &ctx);

    hbi_kv_context_append_tokens(mgr, ctx, 500);

    /* Truncate to 300 */
    hbi_status st = hbi_kv_context_truncate(mgr, ctx, 300);
    ASSERT(st == HBI_OK, "truncate to 300");

    hbi_context_state state;
    hbi_kv_context_get_state(mgr, ctx, &state);
    ASSERT(state.total_tokens == 300, "total_tokens is 300");

    /* Truncate greater than total tokens does nothing */
    st = hbi_kv_context_truncate(mgr, ctx, 400);
    ASSERT(st == HBI_OK, "truncate to 400 (no-op)");
    hbi_kv_context_get_state(mgr, ctx, &state);
    ASSERT(state.total_tokens == 300, "total_tokens is still 300");

    hbi_kv_context_destroy(mgr, ctx);
    hbi_kv_manager_destroy(mgr);
    return 0;
}

static int test_context_clone(void) {
    hbi_kv_manager *mgr = NULL;
    hbi_kv_manager_create(hbi_allocator_system(), NULL, &mgr);

    hbi_context_handle *ctx1 = NULL;
    hbi_shape shape;
    int64_t dims[4] = {3, 32, 128, 64};
    hbi_shape_init(&shape, dims, 4);
    hbi_kv_context_create(mgr, 128, HBI_DTYPE_FP16, &shape, &shape, &ctx1);

    hbi_kv_context_append_tokens(mgr, ctx1, 50);

    /* Clone */
    hbi_context_handle *ctx2 = NULL;
    hbi_status st = hbi_kv_context_clone(mgr, ctx1, &ctx2);
    ASSERT(st == HBI_OK, "context clone");

    hbi_context_state state1, state2;
    hbi_kv_context_get_state(mgr, ctx1, &state1);
    hbi_kv_context_get_state(mgr, ctx2, &state2);

    ASSERT(state2.max_tokens == state1.max_tokens, "clone max_tokens match");
    ASSERT(state2.total_tokens == state1.total_tokens, "clone total_tokens match");

    /* Append to clone, original should be unchanged */
    hbi_kv_context_append_tokens(mgr, ctx2, 10);

    hbi_kv_context_get_state(mgr, ctx1, &state1);
    hbi_kv_context_get_state(mgr, ctx2, &state2);
    ASSERT(state1.total_tokens == 50, "original unchanged");
    ASSERT(state2.total_tokens == 60, "clone appended");

    /* We need to properly initialize the tensor data for a true tensor copy test,
     * but the tensor layer logic inside clone is verified by it not crashing. */

    hbi_kv_context_destroy(mgr, ctx1);
    hbi_kv_context_destroy(mgr, ctx2);
    hbi_kv_manager_destroy(mgr);
    return 0;
}

static int test_selftest(void) {
    ASSERT(hbi_kv_selftest() == HBI_OK, "selftest passes");
    ASSERT(strcmp(hbi_kv_name(), "kv") == 0, "module name");
    return 0;
}

int main(void) {
    hbi_error_clear();

    int failures = 0;

    failures += test_manager_lifecycle();
    failures += test_context_lifecycle_and_append();
    failures += test_context_truncate();
    failures += test_context_clone();
    failures += test_selftest();

    if (failures == 0) {
        printf("[ok] %s\n", hbi_kv_name());
    } else {
        fprintf(stderr, "%d test(s) failed\n", failures);
    }
    return failures;
}
