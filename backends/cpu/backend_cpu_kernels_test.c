/* backend_cpu_kernels_test.c — correctness tests for the CPU reference kernels.
 *
 * These are the correctness gate for the scalar reference implementations
 * (RFC-003, DD-025): each op is dispatched through the kernel runtime and its
 * result checked against a hand-computed answer. Error handling is exercised too
 * — bad shapes, read-only outputs, non-contiguous inputs, and unsupported dtypes
 * must all return an hbi_status and never crash.
 */
#include "backend_cpu_kernels.h"
#include "kernel/kernel.h"

#include "hbi_test.h"

#include <stdint.h>
#include <string.h>

/* Allocate a contiguous fp32 tensor of `rank` dims and fill it from `vals`
 * (row-major). Caller destroys. */
static hbi_status make_fp32(hbi_tensor *t, const int64_t *dims, uint32_t rank, const float *vals) {
    hbi_shape s;
    hbi_status st = hbi_shape_init(&s, dims, rank);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_tensor_alloc(t, HBI_DTYPE_FP32, &s);
    if (st != HBI_OK) {
        return st;
    }
    if (vals != NULL) {
        int64_t n = 0;
        (void)hbi_shape_elem_count(&s, &n);
        memcpy(t->data, vals, (size_t)n * sizeof(float));
    }
    return HBI_OK;
}

static hbi_status make_i8(hbi_tensor *t, const int64_t *dims, uint32_t rank) {
    hbi_shape s;
    hbi_status st = hbi_shape_init(&s, dims, rank);
    if (st != HBI_OK) {
        return st;
    }
    return hbi_tensor_alloc(t, HBI_DTYPE_INT8, &s);
}

/* ── Registration ────────────────────────────────────────────────────────── */

static void test_register(void) {
    hbi_kernel_registry_clear();
    HBI_CHECK(hb_backend_cpu_register_kernels() == HBI_OK);
    /* copy, fill, cast, elementwise, transpose, matmul = 6 kernels. */
    HBI_CHECK_EQ_INT(hbi_kernel_registry_count(), 6);
    /* A second registration without clearing must collide (no shadowing). */
    HBI_CHECK(hb_backend_cpu_register_kernels() == HBI_ERR_STATE);
}

/* ── COPY ────────────────────────────────────────────────────────────────── */

static void test_copy(void) {
    int64_t dims[2] = {2, 3};
    float in_vals[6] = {1, 2, 3, 4, 5, 6};
    hbi_tensor in, out;
    HBI_CHECK(make_fp32(&in, dims, 2, in_vals) == HBI_OK);
    HBI_CHECK(make_fp32(&out, dims, 2, NULL) == HBI_OK);

    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &in;
    args.num_inputs = 1;
    args.outputs[0] = &out;
    args.num_outputs = 1;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    const float *p = (const float *)out.data;
    for (int i = 0; i < 6; ++i) {
        HBI_CHECK(p[i] == in_vals[i]);
    }
    hbi_tensor_destroy(&in);
    hbi_tensor_destroy(&out);
}

/* ── FILL ────────────────────────────────────────────────────────────────── */

static void test_fill(void) {
    int64_t dims[1] = {5};
    hbi_tensor out;
    HBI_CHECK(make_fp32(&out, dims, 1, NULL) == HBI_OK);
    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.outputs[0] = &out;
    args.num_outputs = 1;
    args.params.u.fill_value = -3.5;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_FILL, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    const float *p = (const float *)out.data;
    for (int i = 0; i < 5; ++i) {
        HBI_CHECK(p[i] == -3.5f);
    }
    hbi_tensor_destroy(&out);

    /* int8 fill with clamping (200 -> 127). */
    hbi_tensor o8;
    HBI_CHECK(make_i8(&o8, dims, 1) == HBI_OK);
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.outputs[0] = &o8;
    args.num_outputs = 1;
    args.params.u.fill_value = 200.0;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_FILL, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    const int8_t *q = (const int8_t *)o8.data;
    for (int i = 0; i < 5; ++i) {
        HBI_CHECK_EQ_INT(q[i], 127);
    }
    hbi_tensor_destroy(&o8);
}

/* ── CAST ────────────────────────────────────────────────────────────────── */

static void test_cast(void) {
    int64_t dims[1] = {5};
    float in_vals[5] = {1.4f, -2.6f, 130.0f, -200.0f, 0.5f};
    hbi_tensor in, out;
    HBI_CHECK(make_fp32(&in, dims, 1, in_vals) == HBI_OK);
    HBI_CHECK(make_i8(&out, dims, 1) == HBI_OK);
    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &in;
    args.num_inputs = 1;
    args.outputs[0] = &out;
    args.num_outputs = 1;
    args.params.u.cast_target = HBI_DTYPE_INT8;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_CAST, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    const int8_t *q = (const int8_t *)out.data;
    HBI_CHECK_EQ_INT(q[0], 1);    /* round(1.4) */
    HBI_CHECK_EQ_INT(q[1], -3);   /* round(-2.6) */
    HBI_CHECK_EQ_INT(q[2], 127);  /* clamp(130) */
    HBI_CHECK_EQ_INT(q[3], -128); /* clamp(-200) */
    HBI_CHECK_EQ_INT(q[4], 1);    /* round(0.5) ties away from zero */

    /* Round-trip int8 -> fp32. */
    hbi_tensor back;
    HBI_CHECK(make_fp32(&back, dims, 1, NULL) == HBI_OK);
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &out;
    args.num_inputs = 1;
    args.outputs[0] = &back;
    args.num_outputs = 1;
    args.params.u.cast_target = HBI_DTYPE_FP32;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_CAST, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    const float *b = (const float *)back.data;
    HBI_CHECK(b[0] == 1.0f && b[1] == -3.0f && b[2] == 127.0f && b[3] == -128.0f && b[4] == 1.0f);

    hbi_tensor_destroy(&in);
    hbi_tensor_destroy(&out);
    hbi_tensor_destroy(&back);
}

/* ── ELEMENTWISE ─────────────────────────────────────────────────────────── */

static void test_elementwise(void) {
    int64_t dims[1] = {4};
    float a_vals[4] = {1, 2, 3, 4};
    float b_vals[4] = {10, 20, 30, 40};
    hbi_tensor a, b, c;
    HBI_CHECK(make_fp32(&a, dims, 1, a_vals) == HBI_OK);
    HBI_CHECK(make_fp32(&b, dims, 1, b_vals) == HBI_OK);
    HBI_CHECK(make_fp32(&c, dims, 1, NULL) == HBI_OK);
    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &a;
    args.inputs[1] = &b;
    args.num_inputs = 2;
    args.outputs[0] = &c;
    args.num_outputs = 1;

    args.params.u.elementwise = HBI_ELEMENTWISE_ADD;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_ELEMENTWISE, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    const float *pc = (const float *)c.data;
    HBI_CHECK(pc[0] == 11 && pc[1] == 22 && pc[2] == 33 && pc[3] == 44);

    args.params.u.elementwise = HBI_ELEMENTWISE_MUL;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_ELEMENTWISE, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    HBI_CHECK(pc[0] == 10 && pc[1] == 40 && pc[2] == 90 && pc[3] == 160);

    hbi_tensor_destroy(&a);
    hbi_tensor_destroy(&b);
    hbi_tensor_destroy(&c);
}

/* ── TRANSPOSE ───────────────────────────────────────────────────────────── */

static void test_transpose(void) {
    int64_t idims[2] = {2, 3};
    int64_t odims[2] = {3, 2};
    float in_vals[6] = {1, 2, 3, 4, 5, 6}; /* [[1,2,3],[4,5,6]] */
    hbi_tensor in, out;
    HBI_CHECK(make_fp32(&in, idims, 2, in_vals) == HBI_OK);
    HBI_CHECK(make_fp32(&out, odims, 2, NULL) == HBI_OK);
    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &in;
    args.num_inputs = 1;
    args.outputs[0] = &out;
    args.num_outputs = 1;
    args.params.u.transpose.axis_a = 0;
    args.params.u.transpose.axis_b = 1;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_TRANSPOSE, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    /* Expect [[1,4],[2,5],[3,6]] row-major = 1,4,2,5,3,6. */
    const float *p = (const float *)out.data;
    float want[6] = {1, 4, 2, 5, 3, 6};
    for (int i = 0; i < 6; ++i) {
        HBI_CHECK(p[i] == want[i]);
    }
    hbi_tensor_destroy(&in);
    hbi_tensor_destroy(&out);
}

/* ── MATMUL ──────────────────────────────────────────────────────────────── */

static void test_matmul(void) {
    int64_t ad[2] = {2, 3};
    int64_t bd[2] = {3, 2};
    int64_t cd[2] = {2, 2};
    float a_vals[6] = {1, 2, 3, 4, 5, 6};    /* [[1,2,3],[4,5,6]] */
    float b_vals[6] = {7, 8, 9, 10, 11, 12}; /* [[7,8],[9,10],[11,12]] */
    hbi_tensor a, b, c;
    HBI_CHECK(make_fp32(&a, ad, 2, a_vals) == HBI_OK);
    HBI_CHECK(make_fp32(&b, bd, 2, b_vals) == HBI_OK);
    HBI_CHECK(make_fp32(&c, cd, 2, NULL) == HBI_OK);
    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &a;
    args.inputs[1] = &b;
    args.num_inputs = 2;
    args.outputs[0] = &c;
    args.num_outputs = 1;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_MATMUL, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_OK);
    const float *pc = (const float *)c.data;
    HBI_CHECK(pc[0] == 58.0f);  /* 1*7+2*9+3*11 */
    HBI_CHECK(pc[1] == 64.0f);  /* 1*8+2*10+3*12 */
    HBI_CHECK(pc[2] == 139.0f); /* 4*7+5*9+6*11 */
    HBI_CHECK(pc[3] == 154.0f); /* 4*8+5*10+6*12 */
    hbi_tensor_destroy(&a);
    hbi_tensor_destroy(&b);
    hbi_tensor_destroy(&c);
}

/* ── Error handling (never crash) ────────────────────────────────────────── */

static void test_errors(void) {
    int64_t ad[2] = {2, 3};
    int64_t bad_bd[2] = {2, 2}; /* inner dim 2 != A's K=3 */
    int64_t cd[2] = {2, 2};
    hbi_tensor a, b, c;
    HBI_CHECK(make_fp32(&a, ad, 2, NULL) == HBI_OK);
    HBI_CHECK(make_fp32(&b, bad_bd, 2, NULL) == HBI_OK);
    HBI_CHECK(make_fp32(&c, cd, 2, NULL) == HBI_OK);
    hbi_kernel_args args;
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &a;
    args.inputs[1] = &b;
    args.num_inputs = 2;
    args.outputs[0] = &c;
    args.num_outputs = 1;
    /* Mismatched inner dims → INVALID_ARG, not a crash. */
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_MATMUL, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_ERR_INVALID_ARG);
    hbi_tensor_destroy(&b);

    /* A non-contiguous input (transpose view) → UNSUPPORTED, not a crash. */
    int64_t sq[2] = {3, 3};
    hbi_tensor s, sv, out3;
    HBI_CHECK(make_fp32(&s, sq, 2, NULL) == HBI_OK);
    HBI_CHECK(hbi_tensor_transpose(&s, 0, 1, &sv) == HBI_OK); /* non-contiguous view */
    HBI_CHECK(make_fp32(&out3, sq, 2, NULL) == HBI_OK);
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &sv;
    args.num_inputs = 1;
    args.outputs[0] = &out3;
    args.num_outputs = 1;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_ERR_UNSUPPORTED);
    hbi_tensor_destroy(&s);
    hbi_tensor_destroy(&out3);

    /* A read-only output → STATE error. */
    float buf[6] = {0};
    hbi_shape shp;
    HBI_CHECK(hbi_shape_init(&shp, ad, 2) == HBI_OK);
    hbi_tensor ro;
    HBI_CHECK(hbi_tensor_wrap_readonly(&ro, HBI_DTYPE_FP32, &shp, buf, sizeof(buf)) == HBI_OK);
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.outputs[0] = &ro;
    args.num_outputs = 1;
    args.params.u.fill_value = 1.0;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_FILL, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_ERR_STATE);

    /* An op with no registered kernel for the dtype → NOT_FOUND. matmul on int8
     * has no kernel (only fp32 registered). */
    int64_t md[2] = {2, 2};
    hbi_tensor ai, bi, ci;
    HBI_CHECK(make_i8(&ai, md, 2) == HBI_OK);
    HBI_CHECK(make_i8(&bi, md, 2) == HBI_OK);
    HBI_CHECK(make_i8(&ci, md, 2) == HBI_OK);
    HBI_CHECK(hbi_kernel_args_init(&args) == HBI_OK);
    args.inputs[0] = &ai;
    args.inputs[1] = &bi;
    args.num_inputs = 2;
    args.outputs[0] = &ci;
    args.num_outputs = 1;
    HBI_CHECK(hbi_kernel_dispatch(HBI_KERNEL_OP_MATMUL, HBI_TENSOR_DEVICE_CPU, &args, NULL) ==
              HBI_ERR_NOT_FOUND);

    hbi_tensor_destroy(&a);
    hbi_tensor_destroy(&c);
    hbi_tensor_destroy(&ai);
    hbi_tensor_destroy(&bi);
    hbi_tensor_destroy(&ci);
}

int main(void) {
    HBI_TEST_BEGIN("backend_cpu_kernels");
    HBI_RUN(test_register); /* must run first: populates the registry */
    HBI_RUN(test_copy);
    HBI_RUN(test_fill);
    HBI_RUN(test_cast);
    HBI_RUN(test_elementwise);
    HBI_RUN(test_transpose);
    HBI_RUN(test_matmul);
    HBI_RUN(test_errors);
    return HBI_TEST_END();
}
