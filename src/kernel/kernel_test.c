/* kernel_test.c — unit tests for the kernel runtime INTERFACE.
 *
 * Covers the op taxonomy, the args block, the workspace lifecycle, the kernel
 * registry (register / duplicate-collision / full / clear / count / at), the
 * resolver (op+device+dtype match, layout-flag matching, not-found), and the
 * dispatch path (dtype inference, workspace management, error handling, and the
 * "never crash on bad input" contract). It uses a tiny locally-defined dummy
 * kernel so it tests the interface with NO dependency on any real backend — the
 * CPU reference kernels are correctness-tested in backends/cpu.
 */
#include "kernel/kernel.h"

#include "hbi_test.h"

#include <stdint.h>
#include <string.h>

/* ── A trivial dummy kernel for exercising the registry/dispatch ──────────────
 * "dummy.copy.fp32" copies input[0] into output[0] element-wise (fp32), and
 * records that it ran + how much workspace it was handed. It validates its args
 * so we can test the never-crash contract. */
static int g_dummy_runs;
static size_t g_dummy_saw_capacity;

static const hbi_dtype k_dummy_dtypes[] = {HBI_DTYPE_FP32};

static hbi_status dummy_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    if (args == NULL || args->num_inputs != 1 || args->num_outputs != 1 ||
        args->inputs[0] == NULL || args->outputs[0] == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "dummy_run: bad args");
    }
    ++g_dummy_runs;
    g_dummy_saw_capacity = hbi_kernel_workspace_capacity(ws);
    const hbi_tensor *in = args->inputs[0];
    hbi_tensor *out = args->outputs[0];
    if (in->dtype != HBI_DTYPE_FP32 || out->dtype != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "dummy_run: dtype");
    }
    memcpy(out->data, in->data, in->nbytes < out->nbytes ? in->nbytes : out->nbytes);
    return HBI_OK;
}

/* Reports a fixed 128-byte workspace need so we can observe dispatch honoring
 * the workspace_size hook. */
static hbi_status dummy_ws_size(const hbi_kernel_args *args, size_t *out_bytes) {
    (void)args;
    *out_bytes = 128u;
    return HBI_OK;
}

static const hbi_kernel k_dummy_copy = {
    .op = HBI_KERNEL_OP_COPY,
    .name = "dummy.copy.fp32",
    .device = HBI_TENSOR_DEVICE_CPU,
    .supported_dtypes = k_dummy_dtypes,
    .num_dtypes = 1u,
    .layout_flags = HBI_KERNEL_LAYOUT_ANY,
    .workspace_size = dummy_ws_size,
    .run = dummy_run,
};

/* A second kernel for a different op with no workspace need. */
static hbi_status noop_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)args;
    (void)ws;
    return HBI_OK;
}
static const hbi_kernel k_dummy_fill = {
    .op = HBI_KERNEL_OP_FILL,
    .name = "dummy.fill.fp32",
    .device = HBI_TENSOR_DEVICE_CPU,
    .supported_dtypes = k_dummy_dtypes,
    .num_dtypes = 1u,
    .layout_flags = HBI_KERNEL_LAYOUT_ANY,
    .workspace_size = NULL, /* NULL == always 0 workspace */
    .run = noop_run,
};

/* Make a fresh 1-D fp32 tensor of `n` elements. Caller destroys. */
static hbi_status make_vec(hbi_tensor *t, int64_t n) {
    hbi_shape s;
    int64_t dims[1] = {n};
    hbi_status st = hbi_shape_init(&s, dims, 1);
    if (st != HBI_OK) {
        return st;
    }
    return hbi_tensor_alloc(t, HBI_DTYPE_FP32, &s);
}

/* ── Op taxonomy ─────────────────────────────────────────────────────────── */

static void test_op_strings(void) {
    HBI_CHECK_STR_EQ(hbi_kernel_op_str(HBI_KERNEL_OP_MATMUL), "matmul");
    HBI_CHECK_STR_EQ(hbi_kernel_op_str(HBI_KERNEL_OP_COPY), "copy");
    HBI_CHECK_STR_EQ(hbi_kernel_op_str(HBI_KERNEL_OP_ELEMENTWISE), "elementwise");
    /* Sentinels spell "invalid" and are not valid ops. */
    HBI_CHECK_STR_EQ(hbi_kernel_op_str(HBI_KERNEL_OP_INVALID), "invalid");
    HBI_CHECK_STR_EQ(hbi_kernel_op_str(HBI_KERNEL_OP_COUNT), "invalid");
    HBI_CHECK(!hbi_kernel_op_is_valid(HBI_KERNEL_OP_INVALID));
    HBI_CHECK(!hbi_kernel_op_is_valid(HBI_KERNEL_OP_COUNT));
    /* Every real op has a distinct, non-"invalid" name and reports valid. */
    for (int op = HBI_KERNEL_OP_INVALID + 1; op < HBI_KERNEL_OP_COUNT; ++op) {
        const char *s = hbi_kernel_op_str((hbi_kernel_op)op);
        HBI_CHECK(s != NULL && strcmp(s, "invalid") != 0);
        HBI_CHECK(hbi_kernel_op_is_valid((hbi_kernel_op)op));
    }
}

/* ── Args ────────────────────────────────────────────────────────────────── */

static void test_args_init(void) {
    hbi_kernel_args args;
    memset(&args, 0xEE, sizeof(args)); /* poison first */
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    HBI_CHECK_EQ_INT(args.num_inputs, 0);
    HBI_CHECK_EQ_INT(args.num_outputs, 0);
    HBI_CHECK(hbi_kernel_args_init(NULL) == HBI_ERR_INVALID_ARG);
}

/* ── Workspace ───────────────────────────────────────────────────────────── */

static void test_workspace_lifecycle(void) {
    hbi_kernel_workspace ws;
    HBI_CHECK(hbi_kernel_workspace_init(&ws, NULL) == HBI_OK);
    HBI_CHECK(hbi_kernel_workspace_ptr(&ws) == NULL);
    HBI_CHECK_EQ_INT((long long)hbi_kernel_workspace_capacity(&ws), 0);

    /* reserve grows and aligns. */
    HBI_CHECK(hbi_kernel_workspace_reserve(&ws, 256, 64) == HBI_OK);
    void *p = hbi_kernel_workspace_ptr(&ws);
    HBI_CHECK(p != NULL);
    HBI_CHECK(((uintptr_t)p % 64u) == 0u);
    HBI_CHECK(hbi_kernel_workspace_capacity(&ws) >= 256u);

    /* A smaller request reuses the same buffer (no reallocation). */
    HBI_CHECK(hbi_kernel_workspace_reserve(&ws, 128, 64) == HBI_OK);
    HBI_CHECK(hbi_kernel_workspace_ptr(&ws) == p);

    /* A larger request may move the buffer but must still satisfy alignment. */
    HBI_CHECK(hbi_kernel_workspace_reserve(&ws, 4096, 64) == HBI_OK);
    HBI_CHECK(((uintptr_t)hbi_kernel_workspace_ptr(&ws) % 64u) == 0u);
    HBI_CHECK(hbi_kernel_workspace_capacity(&ws) >= 4096u);

    /* reset releases the buffer but keeps the binding; can reserve again. */
    hbi_kernel_workspace_reset(&ws);
    HBI_CHECK(hbi_kernel_workspace_ptr(&ws) == NULL);
    HBI_CHECK(hbi_kernel_workspace_reserve(&ws, 64, 0) == HBI_OK);
    HBI_CHECK(hbi_kernel_workspace_ptr(&ws) != NULL);

    hbi_kernel_workspace_destroy(&ws);
    HBI_CHECK(hbi_kernel_workspace_ptr(&ws) == NULL);
}

static void test_workspace_errors(void) {
    hbi_kernel_workspace ws;
    HBI_CHECK(hbi_kernel_workspace_init(&ws, NULL) == HBI_OK);
    /* bytes == 0 is a no-op success (nothing to guarantee). */
    HBI_CHECK(hbi_kernel_workspace_reserve(&ws, 0, 64) == HBI_OK);
    HBI_CHECK(hbi_kernel_workspace_ptr(&ws) == NULL);
    /* non-power-of-two alignment is rejected. */
    HBI_CHECK(hbi_kernel_workspace_reserve(&ws, 64, 48) == HBI_ERR_INVALID_ARG);
    /* NULL ws is rejected, never crashes. */
    HBI_CHECK(hbi_kernel_workspace_init(NULL, NULL) == HBI_ERR_INVALID_ARG);
    HBI_CHECK(hbi_kernel_workspace_reserve(NULL, 64, 64) == HBI_ERR_INVALID_ARG);
    /* NULL-safe teardown/accessors. */
    hbi_kernel_workspace_reset(NULL);
    hbi_kernel_workspace_destroy(NULL);
    HBI_CHECK(hbi_kernel_workspace_ptr(NULL) == NULL);
    HBI_CHECK_EQ_INT((long long)hbi_kernel_workspace_capacity(NULL), 0);
    hbi_kernel_workspace_destroy(&ws);
}

/* ── Registry ────────────────────────────────────────────────────────────── */

static void test_register_and_query(void) {
    hbi_kernel_registry_clear();
    HBI_CHECK_EQ_INT(hbi_kernel_registry_count(), 0);
    HBI_CHECK(hbi_kernel_at(0) == NULL);
    HBI_CHECK(hbi_kernel_at(-1) == NULL);

    HBI_CHECK(hbi_kernel_register(&k_dummy_copy) == HBI_OK);
    HBI_CHECK(hbi_kernel_register(&k_dummy_fill) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_kernel_registry_count(), 2);
    HBI_CHECK(hbi_kernel_at(0) == &k_dummy_copy);
    HBI_CHECK(hbi_kernel_at(1) == &k_dummy_fill);
    HBI_CHECK(hbi_kernel_at(2) == NULL);

    HBI_CHECK(hbi_kernel_supports_dtype(&k_dummy_copy, HBI_DTYPE_FP32));
    HBI_CHECK(!hbi_kernel_supports_dtype(&k_dummy_copy, HBI_DTYPE_INT8));
    HBI_CHECK(!hbi_kernel_supports_dtype(NULL, HBI_DTYPE_FP32));

    hbi_kernel_registry_clear();
    HBI_CHECK_EQ_INT(hbi_kernel_registry_count(), 0);
}

static void test_register_validation(void) {
    hbi_kernel_registry_clear();
    /* NULL kernel / NULL run / NULL name. */
    HBI_CHECK(hbi_kernel_register(NULL) == HBI_ERR_INVALID_ARG);
    hbi_kernel bad = k_dummy_copy;
    bad.run = NULL;
    HBI_CHECK(hbi_kernel_register(&bad) == HBI_ERR_INVALID_ARG);
    bad = k_dummy_copy;
    bad.name = NULL;
    HBI_CHECK(hbi_kernel_register(&bad) == HBI_ERR_INVALID_ARG);
    /* invalid op. */
    bad = k_dummy_copy;
    bad.op = HBI_KERNEL_OP_INVALID;
    HBI_CHECK(hbi_kernel_register(&bad) == HBI_ERR_INVALID_ARG);
    /* empty dtype set. */
    bad = k_dummy_copy;
    bad.num_dtypes = 0;
    HBI_CHECK(hbi_kernel_register(&bad) == HBI_ERR_INVALID_ARG);
    bad = k_dummy_copy;
    bad.supported_dtypes = NULL;
    HBI_CHECK(hbi_kernel_register(&bad) == HBI_ERR_INVALID_ARG);
    HBI_CHECK_EQ_INT(hbi_kernel_registry_count(), 0);
}

static void test_register_collision(void) {
    hbi_kernel_registry_clear();
    HBI_CHECK(hbi_kernel_register(&k_dummy_copy) == HBI_OK);
    /* A second kernel for the same (op, device, dtype, layout) must be refused —
     * no silent shadowing that would make resolution order-dependent. */
    hbi_kernel clash = k_dummy_copy;
    clash.name = "dummy.copy.fp32.dup";
    HBI_CHECK(hbi_kernel_register(&clash) == HBI_ERR_STATE);
    HBI_CHECK_EQ_INT(hbi_kernel_registry_count(), 1);
    /* Same op+dtype but a DIFFERENT layout requirement is allowed to coexist. */
    hbi_kernel contig = k_dummy_copy;
    contig.name = "dummy.copy.fp32.contig";
    contig.layout_flags = HBI_KERNEL_LAYOUT_REQUIRE_CONTIGUOUS;
    HBI_CHECK(hbi_kernel_register(&contig) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_kernel_registry_count(), 2);
    hbi_kernel_registry_clear();
}

/* ── Resolve ─────────────────────────────────────────────────────────────── */

static void test_resolve(void) {
    hbi_kernel_registry_clear();
    HBI_CHECK(hbi_kernel_register(&k_dummy_copy) == HBI_OK);

    hbi_kernel_key key = {
        .op = HBI_KERNEL_OP_COPY,
        .device = HBI_TENSOR_DEVICE_CPU,
        .dtype = HBI_DTYPE_FP32,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
    };
    const hbi_kernel *k = NULL;
    HBI_CHECK(hbi_kernel_resolve(&key, &k) == HBI_OK);
    HBI_CHECK(k == &k_dummy_copy);

    /* Wrong dtype → not found. */
    key.dtype = HBI_DTYPE_INT8;
    HBI_CHECK(hbi_kernel_resolve(&key, &k) == HBI_ERR_NOT_FOUND);
    key.dtype = HBI_DTYPE_FP32;

    /* Wrong op → not found. */
    key.op = HBI_KERNEL_OP_MATMUL;
    HBI_CHECK(hbi_kernel_resolve(&key, &k) == HBI_ERR_NOT_FOUND);
    key.op = HBI_KERNEL_OP_COPY;

    /* Invalid op / NULL args → invalid arg (never crash). */
    key.op = HBI_KERNEL_OP_INVALID;
    HBI_CHECK(hbi_kernel_resolve(&key, &k) == HBI_ERR_INVALID_ARG);
    HBI_CHECK(hbi_kernel_resolve(NULL, &k) == HBI_ERR_INVALID_ARG);
    key.op = HBI_KERNEL_OP_COPY;
    HBI_CHECK(hbi_kernel_resolve(&key, NULL) == HBI_ERR_INVALID_ARG);

    hbi_kernel_registry_clear();
}

static void test_resolve_layout_flags(void) {
    hbi_kernel_registry_clear();
    /* Register a contiguity-requiring kernel only. */
    hbi_kernel contig = k_dummy_copy;
    contig.name = "dummy.copy.fp32.contig";
    contig.layout_flags = HBI_KERNEL_LAYOUT_REQUIRE_CONTIGUOUS;
    HBI_CHECK(hbi_kernel_register(&contig) == HBI_OK);

    hbi_kernel_key key = {
        .op = HBI_KERNEL_OP_COPY,
        .device = HBI_TENSOR_DEVICE_CPU,
        .dtype = HBI_DTYPE_FP32,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
    };
    /* A key that does NOT promise contiguity cannot use a kernel that REQUIRES
     * it — resolution must miss (the kernel would compute the wrong thing). */
    const hbi_kernel *k = NULL;
    HBI_CHECK(hbi_kernel_resolve(&key, &k) == HBI_ERR_NOT_FOUND);
    /* A key that promises contiguity resolves to it. */
    key.layout_flags = HBI_KERNEL_LAYOUT_REQUIRE_CONTIGUOUS;
    HBI_CHECK(hbi_kernel_resolve(&key, &k) == HBI_OK);
    HBI_CHECK(k == &contig);

    hbi_kernel_registry_clear();
}

/* ── Dispatch ────────────────────────────────────────────────────────────── */

static void test_dispatch_runs_kernel(void) {
    hbi_kernel_registry_clear();
    HBI_CHECK(hbi_kernel_register(&k_dummy_copy) == HBI_OK);
    g_dummy_runs = 0;
    g_dummy_saw_capacity = 0;

    hbi_tensor in, out;
    HBI_CHECK(make_vec(&in, 8) == HBI_OK);
    HBI_CHECK(make_vec(&out, 8) == HBI_OK);
    float *pin = (float *)in.data;
    for (int i = 0; i < 8; ++i) {
        pin[i] = (float)(i + 1);
    }

    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &in;
    args.num_inputs = 1;
    args.outputs[0] = &out;
    args.num_outputs = 1;

    /* No workspace passed: dispatch must build a temporary one because the
     * kernel reports a 128-byte need. */
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    HBI_CHECK_EQ_INT(g_dummy_runs, 1);
    HBI_CHECK(g_dummy_saw_capacity >= 128u);
    const float *pout = (const float *)out.data;
    for (int i = 0; i < 8; ++i) {
        HBI_CHECK(pout[i] == (float)(i + 1));
    }

    hbi_tensor_destroy(&in);
    hbi_tensor_destroy(&out);
    hbi_kernel_registry_clear();
}

static void test_dispatch_with_workspace(void) {
    hbi_kernel_registry_clear();
    HBI_CHECK(hbi_kernel_register(&k_dummy_copy) == HBI_OK);
    g_dummy_runs = 0;

    hbi_tensor in, out;
    HBI_CHECK(make_vec(&in, 4) == HBI_OK);
    HBI_CHECK(make_vec(&out, 4) == HBI_OK);

    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &in;
    args.num_inputs = 1;
    args.outputs[0] = &out;
    args.num_outputs = 1;

    /* A caller-provided workspace is reused across calls. */
    hbi_kernel_workspace ws;
    HBI_CHECK(hbi_kernel_workspace_init(&ws, NULL) == HBI_OK);
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &args, &ws) == HBI_OK);
    HBI_CHECK(hbi_kernel_workspace_capacity(&ws) >= 128u);
    void *first = hbi_kernel_workspace_ptr(&ws);
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &args, &ws) == HBI_OK);
    HBI_CHECK(hbi_kernel_workspace_ptr(&ws) == first); /* reused, not realloced */
    HBI_CHECK_EQ_INT(g_dummy_runs, 2);

    hbi_kernel_workspace_destroy(&ws);
    hbi_tensor_destroy(&in);
    hbi_tensor_destroy(&out);
    hbi_kernel_registry_clear();
}

static void test_dispatch_errors(void) {
    hbi_kernel_registry_clear();
    /* No kernel registered → NOT_FOUND (dtype inferred from the input). */
    hbi_tensor in, out;
    HBI_CHECK(make_vec(&in, 4) == HBI_OK);
    HBI_CHECK(make_vec(&out, 4) == HBI_OK);
    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &in;
    args.num_inputs = 1;
    args.outputs[0] = &out;
    args.num_outputs = 1;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_ERR_NOT_FOUND);

    /* NULL args → invalid arg, no crash. */
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, NULL, NULL) ==
              HBI_ERR_INVALID_ARG);

    /* Operand count out of range → invalid arg. */
    hbi_kernel_args over;
    HBI_CHECK(hbi_kernel_args_init(&over) == HBI_OK);
    over.num_inputs = HBI_KERNEL_MAX_INPUTS + 1u;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &over, NULL) ==
              HBI_ERR_INVALID_ARG);

    /* No tensor to infer a dtype from → invalid arg. */
    hbi_kernel_args empty;
    HBI_CHECK(hbi_kernel_args_init(&empty) == HBI_OK);
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &empty, NULL) ==
              HBI_ERR_INVALID_ARG);

    hbi_tensor_destroy(&in);
    hbi_tensor_destroy(&out);
    hbi_kernel_registry_clear();
}

/* ── Module identity / self-test ─────────────────────────────────────────── */

static void test_module_identity(void) {
    HBI_CHECK_STR_EQ(hbi_kernel_name(), "kernel");
    /* The selftest is registry-independent, so it holds even mid-suite. */
    hbi_kernel_registry_clear();
    HBI_CHECK(hbi_kernel_selftest() == HBI_OK);
}

int main(void) {
    HBI_TEST_BEGIN("kernel");
    HBI_RUN(test_op_strings);
    HBI_RUN(test_args_init);
    HBI_RUN(test_workspace_lifecycle);
    HBI_RUN(test_workspace_errors);
    HBI_RUN(test_register_and_query);
    HBI_RUN(test_register_validation);
    HBI_RUN(test_register_collision);
    HBI_RUN(test_resolve);
    HBI_RUN(test_resolve_layout_flags);
    HBI_RUN(test_dispatch_runs_kernel);
    HBI_RUN(test_dispatch_with_workspace);
    HBI_RUN(test_dispatch_errors);
    HBI_RUN(test_module_identity);
    return HBI_TEST_END();
}
