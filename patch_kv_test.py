import sys

def modify():
    with open('src/kv/kv_test.c', 'r') as f:
        content = f.read()

    new_tests = """
static int test_many_contexts(void) {
    hbi_kv_manager *mgr = NULL;
    hbi_status st = hbi_kv_manager_create(hbi_allocator_system(), NULL, &mgr);
    ASSERT(st == HBI_OK, "manager create");

    hbi_context_handle *ctxs[64];
    hbi_shape shape;
    int64_t dims[4] = {1, 1, 512, 64};
    hbi_shape_init(&shape, dims, 4);

    for (int i = 0; i < 64; ++i) {
        st = hbi_kv_context_create(mgr, 512, HBI_DTYPE_FP16, &shape, &shape, &ctxs[i]);
        ASSERT(st == HBI_OK, "context create success");
        st = hbi_kv_context_append_tokens(mgr, ctxs[i], 10);
        ASSERT(st == HBI_OK, "context append success");
    }

    hbi_kv_statistics stats;
    hbi_kv_manager_get_statistics(mgr, &stats);
    ASSERT(stats.active_contexts == 64, "active contexts == 64");

    for (int i = 0; i < 64; ++i) {
        hbi_kv_context_destroy(mgr, ctxs[i]);
    }

    hbi_kv_manager_get_statistics(mgr, &stats);
    ASSERT(stats.active_contexts == 0, "active contexts == 0");
    ASSERT(stats.peak_contexts == 64, "peak contexts == 64");

    hbi_kv_manager_destroy(mgr);
    return 0;
}

static int test_oom_behavior(void) {
    hbi_kv_manager *mgr = NULL;
    hbi_status st = hbi_kv_manager_create(hbi_allocator_system(), NULL, &mgr);
    ASSERT(st == HBI_OK, "manager create");

    hbi_context_handle *ctx_large = NULL;
    hbi_shape shape;
    int64_t dims[4] = {1, 32, 1024, 64};
    hbi_shape_init(&shape, dims, 4);
    
    st = hbi_kv_context_create(mgr, UINT32_MAX / 2, HBI_DTYPE_FP16, &shape, &shape, &ctx_large);
    ASSERT(st == HBI_ERR_OOM, "large context should fail with OOM");
    
    hbi_context_handle *ctx_small = NULL;
    st = hbi_kv_context_create(mgr, 128, HBI_DTYPE_FP16, &shape, &shape, &ctx_small);
    ASSERT(st == HBI_OK, "small context should succeed after OOM");
    
    hbi_kv_context_destroy(mgr, ctx_small);
    hbi_kv_manager_destroy(mgr);
    return 0;
}

static int test_resize_grow_shrink(void) {
    hbi_kv_manager *mgr = NULL;
    hbi_status st = hbi_kv_manager_create(hbi_allocator_system(), NULL, &mgr);
    ASSERT(st == HBI_OK, "manager create");

    hbi_context_handle *ctx = NULL;
    hbi_shape shape;
    int64_t dims[4] = {1, 1, 256, 64};
    hbi_shape_init(&shape, dims, 4);

    st = hbi_kv_context_create(mgr, 256, HBI_DTYPE_FP16, &shape, &shape, &ctx);
    ASSERT(st == HBI_OK, "context create");

    st = hbi_kv_context_append_tokens(mgr, ctx, 100);
    ASSERT(st == HBI_OK, "context append");

    st = hbi_kv_context_resize(mgr, ctx, 512);
    ASSERT(st == HBI_OK, "context resize grow");

    hbi_context_state state;
    hbi_kv_context_get_state(mgr, ctx, &state);
    ASSERT(state.total_tokens == 100, "tokens still there after grow");
    ASSERT(state.max_tokens == 512, "max_tokens is 512");

    st = hbi_kv_context_resize(mgr, ctx, 50);
    ASSERT(st == HBI_OK, "context resize shrink");
    hbi_kv_context_get_state(mgr, ctx, &state);
    ASSERT(state.total_tokens == 50, "tokens truncated to 50");
    ASSERT(state.max_tokens == 50, "max_tokens is 50");

    hbi_kv_context_destroy(mgr, ctx);
    hbi_kv_manager_destroy(mgr);
    return 0;
}

static int test_clone(void) {
    hbi_kv_manager *mgr = NULL;
    hbi_status st = hbi_kv_manager_create(hbi_allocator_system(), NULL, &mgr);
    ASSERT(st == HBI_OK, "manager create");

    hbi_context_handle *ctx1 = NULL;
    hbi_shape shape;
    int64_t dims[4] = {1, 1, 128, 64};
    hbi_shape_init(&shape, dims, 4);
    hbi_kv_context_create(mgr, 128, HBI_DTYPE_FP16, &shape, &shape, &ctx1);
    hbi_kv_context_append_tokens(mgr, ctx1, 100);

    hbi_context_handle *ctx2 = NULL;
    st = hbi_kv_context_clone(mgr, ctx1, &ctx2);
    ASSERT(st == HBI_OK, "clone success");

    hbi_context_state state;
    hbi_kv_context_get_state(mgr, ctx2, &state);
    ASSERT(state.total_tokens == 100, "clone has same token count");

    hbi_kv_context_destroy(mgr, ctx1);

    hbi_kv_context_get_state(mgr, ctx2, &state);
    ASSERT(state.total_tokens == 100, "clone valid after original destroyed");
    
    st = hbi_kv_context_append_tokens(mgr, ctx2, 10);
    ASSERT(st == HBI_OK, "clone append after original destroyed");

    hbi_kv_context_destroy(mgr, ctx2);
    hbi_kv_manager_destroy(mgr);
    return 0;
}

static void concurrent_thread_fn(void *arg) {
    hbi_kv_manager *mgr = (hbi_kv_manager *)arg;
    hbi_context_handle *ctxs[16];
    hbi_shape shape;
    int64_t dims[4] = {1, 1, 128, 64};
    hbi_shape_init(&shape, dims, 4);

    for (int i = 0; i < 16; ++i) {
        hbi_status st = hbi_kv_context_create(mgr, 128, HBI_DTYPE_FP16, &shape, &shape, &ctxs[i]);
        if (st != HBI_OK) return;
        st = hbi_kv_context_append_tokens(mgr, ctxs[i], 50);
        if (st != HBI_OK) return;
        
        hbi_context_state state;
        hbi_kv_context_get_state(mgr, ctxs[i], &state);
        if (state.total_tokens != 50) return;
    }

    for (int i = 0; i < 16; ++i) {
        hbi_kv_context_destroy(mgr, ctxs[i]);
    }
}

static int test_concurrent_contexts(void) {
    hbi_kv_manager *mgr = NULL;
    hbi_status st = hbi_kv_manager_create(hbi_allocator_system(), NULL, &mgr);
    ASSERT(st == HBI_OK, "manager create");

    hbi_thread *threads[4];
    for (int i = 0; i < 4; ++i) {
        st = hbi_thread_create(&threads[i], concurrent_thread_fn, mgr);
        ASSERT(st == HBI_OK, "thread create");
    }

    for (int i = 0; i < 4; ++i) {
        hbi_thread_join(threads[i]);
    }

    hbi_kv_statistics stats;
    hbi_kv_manager_get_statistics(mgr, &stats);
    ASSERT(stats.active_contexts == 0, "active contexts == 0 after concurrent test");

    hbi_kv_manager_destroy(mgr);
    return 0;
}
"""

    main_additions = """
    failures += test_many_contexts();
    failures += test_oom_behavior();
    failures += test_resize_grow_shrink();
    failures += test_clone();
    failures += test_concurrent_contexts();
"""

    content = content.replace('static int test_selftest(void) {', new_tests + '\nstatic int test_selftest(void) {')
    content = content.replace('failures += test_context_clone();', 'failures += test_context_clone();' + main_additions)
    content = content.replace('#include "tensor/tensor.h"\n', '#include "tensor/tensor.h"\n#include "platform/platform.h"\n')

    with open('src/kv/kv_test.c', 'w') as f:
        f.write(content)

if __name__ == '__main__':
    modify()
