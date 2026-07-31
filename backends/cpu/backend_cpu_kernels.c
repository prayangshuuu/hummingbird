/* backend_cpu_kernels.c — CPU reference kernels for the kernel runtime.
 *
 * The correctness baseline (RFC-003, DD-025). These are deliberately simple
 * scalar implementations: no SIMD, no blocking, no fast paths. Their only job is
 * to be provably correct so later optimized kernels (and GPU backends) have a
 * reference to validate against. Each kernel:
 *   - operates on C-contiguous operands (returns HBI_ERR_UNSUPPORTED otherwise —
 *     a reference kernel does not attempt strided layouts yet),
 *   - validates operand counts, dtypes, and shape compatibility,
 *   - returns an hbi_status and NEVER crashes on bad input (DD-011/DD-019),
 *   - needs zero workspace (workspace_size == NULL).
 *
 * They register into the `kernel` module's registry via
 * hb_backend_cpu_register_kernels(). The kernel module owns the dispatch surface;
 * the implementations live here, in the backend, exactly as DD-025 prescribes.
 */
#include "kernel/kernel.h"

#include "backend_cpu_kernels.h"

#include <math.h>
#include <string.h>

/* ── Shared validation helpers ───────────────────────────────────────────── */

/* A reference kernel only handles contiguous operands for now. */
static hbi_status require_contiguous(const hbi_tensor *t, const char *what) {
    if (t == NULL) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "%s: NULL tensor", what);
    }
    if (!hbi_tensor_is_contiguous(t)) {
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "%s: non-contiguous (reference kernel)", what);
    }
    if (hbi_tensor_cdata(t) == NULL) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "%s: NULL data", what);
    }
    return HBI_OK;
}

/* Element count of a tensor's shape (>= 1 for a scalar). */
static hbi_status elem_count(const hbi_tensor *t, int64_t *out) {
    return hbi_shape_elem_count(hbi_tensor_shape(t), out);
}

/* Exactly `ni` inputs and `no` outputs, none NULL. */
static hbi_status require_arity(const hbi_kernel_args *args, uint32_t ni, uint32_t no,
                                const char *what) {
    if (args == NULL) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "%s: NULL args", what);
    }
    if (args->num_inputs != ni || args->num_outputs != no) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "%s: expected %u in / %u out, got %u / %u",
                            what, ni, no, args->num_inputs, args->num_outputs);
    }
    for (uint32_t i = 0; i < ni; ++i) {
        if (args->inputs[i] == NULL) {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "%s: NULL input %u", what, i);
        }
    }
    for (uint32_t i = 0; i < no; ++i) {
        if (args->outputs[i] == NULL) {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "%s: NULL output %u", what, i);
        }
        if (hbi_tensor_is_readonly(args->outputs[i])) {
            return HBI_ERR_SETF(HBI_ERR_STATE, 0, "%s: output %u is read-only", what, i);
        }
    }
    return HBI_OK;
}

/* Round-and-clamp a float to the int8 range, matching a symmetric quantizer's
 * rounding (nearest, ties away from zero via roundf). */
static int8_t f32_to_i8(float v) {
    float r = roundf(v);
    if (r > 127.0f) {
        r = 127.0f;
    } else if (r < -128.0f) {
        r = -128.0f;
    }
    return (int8_t)r;
}

/* ── COPY ────────────────────────────────────────────────────────────────────
 * out[i] = in[i]; identical dtype and shape. Works for any byte-addressable
 * dtype (fp32, int8) via a flat byte copy over the contiguous buffer. */
static hbi_status cpu_copy_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 1, 1, "copy");
    if (st != HBI_OK) {
        return st;
    }
    const hbi_tensor *in = args->inputs[0];
    hbi_tensor *out = args->outputs[0];
    if ((st = require_contiguous(in, "copy.in")) != HBI_OK) {
        return st;
    }
    if ((st = require_contiguous(out, "copy.out")) != HBI_OK) {
        return st;
    }
    if (hbi_tensor_dtype(in) != hbi_tensor_dtype(out)) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "copy: dtype mismatch");
    }
    if (!hbi_shape_equal(hbi_tensor_shape(in), hbi_tensor_shape(out))) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "copy: shape mismatch");
    }
    size_t n = hbi_tensor_nbytes(in);
    if (hbi_tensor_nbytes(out) < n) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "copy: output too small");
    }
    memcpy(hbi_tensor_data_mut(out), hbi_tensor_cdata(in), n);
    return HBI_OK;
}

/* ── FILL ────────────────────────────────────────────────────────────────────
 * out[i] = params.fill_value, for every element. Supports fp32 and int8. */
static hbi_status cpu_fill_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 0, 1, "fill");
    if (st != HBI_OK) {
        return st;
    }
    hbi_tensor *out = args->outputs[0];
    if ((st = require_contiguous(out, "fill.out")) != HBI_OK) {
        return st;
    }
    int64_t n = 0;
    if ((st = elem_count(out, &n)) != HBI_OK) {
        return st;
    }
    double value = args->params.u.fill_value;
    switch (hbi_tensor_dtype(out)) {
    case HBI_DTYPE_FP32: {
        float *p = (float *)hbi_tensor_data_mut(out);
        float fv = (float)value;
        for (int64_t i = 0; i < n; ++i) {
            p[i] = fv;
        }
        return HBI_OK;
    }
    case HBI_DTYPE_INT8: {
        int8_t *p = (int8_t *)hbi_tensor_data_mut(out);
        int8_t iv = f32_to_i8((float)value);
        for (int64_t i = 0; i < n; ++i) {
            p[i] = iv;
        }
        return HBI_OK;
    }
    default:
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "fill: dtype %s (reference kernel)",
                            hbi_dtype_str(hbi_tensor_dtype(out)));
    }
}

/* ── CAST ─────────────────────────────────────────────────────────────────────
 * out[i] = convert(in[i]) with out's dtype == params.cast_target. Reference
 * conversions: fp32<->int8 (round+clamp widening/narrowing) and same-dtype
 * passthrough. */
static hbi_status cpu_cast_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 1, 1, "cast");
    if (st != HBI_OK) {
        return st;
    }
    const hbi_tensor *in = args->inputs[0];
    hbi_tensor *out = args->outputs[0];
    if ((st = require_contiguous(in, "cast.in")) != HBI_OK) {
        return st;
    }
    if ((st = require_contiguous(out, "cast.out")) != HBI_OK) {
        return st;
    }
    if (hbi_tensor_dtype(out) != args->params.u.cast_target) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "cast: output dtype != cast_target");
    }
    if (!hbi_shape_equal(hbi_tensor_shape(in), hbi_tensor_shape(out))) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "cast: shape mismatch");
    }
    int64_t n = 0;
    if ((st = elem_count(in, &n)) != HBI_OK) {
        return st;
    }
    hbi_dtype src = hbi_tensor_dtype(in);
    hbi_dtype dst = hbi_tensor_dtype(out);
    const void *sp = hbi_tensor_cdata(in);
    void *dp = hbi_tensor_data_mut(out);

    if (src == HBI_DTYPE_FP32 && dst == HBI_DTYPE_INT8) {
        const float *s = (const float *)sp;
        int8_t *d = (int8_t *)dp;
        for (int64_t i = 0; i < n; ++i) {
            d[i] = f32_to_i8(s[i]);
        }
        return HBI_OK;
    }
    if (src == HBI_DTYPE_INT8 && dst == HBI_DTYPE_FP32) {
        const int8_t *s = (const int8_t *)sp;
        float *d = (float *)dp;
        for (int64_t i = 0; i < n; ++i) {
            d[i] = (float)s[i];
        }
        return HBI_OK;
    }
    if (src == dst && (src == HBI_DTYPE_FP32 || src == HBI_DTYPE_INT8)) {
        memcpy(dp, sp, hbi_tensor_nbytes(in));
        return HBI_OK;
    }
    return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "cast: %s -> %s (reference kernel)",
                        hbi_dtype_str(src), hbi_dtype_str(dst));
}

/* ── ELEMENTWISE (add / mul) ─────────────────────────────────────────────────
 * out[i] = a[i] (+|*) b[i]; equal shapes (no broadcasting in the reference),
 * fp32. The kind is params.elementwise. */
static hbi_status cpu_elementwise_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 2, 1, "elementwise");
    if (st != HBI_OK) {
        return st;
    }
    const hbi_tensor *a = args->inputs[0];
    const hbi_tensor *b = args->inputs[1];
    hbi_tensor *c = args->outputs[0];
    if ((st = require_contiguous(a, "elementwise.a")) != HBI_OK) {
        return st;
    }
    if ((st = require_contiguous(b, "elementwise.b")) != HBI_OK) {
        return st;
    }
    if ((st = require_contiguous(c, "elementwise.out")) != HBI_OK) {
        return st;
    }
    if (hbi_tensor_dtype(a) != HBI_DTYPE_FP32 || hbi_tensor_dtype(b) != HBI_DTYPE_FP32 ||
        hbi_tensor_dtype(c) != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "elementwise: fp32 only (reference kernel)");
    }
    if (!hbi_shape_equal(hbi_tensor_shape(a), hbi_tensor_shape(b)) ||
        !hbi_shape_equal(hbi_tensor_shape(a), hbi_tensor_shape(c))) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "elementwise: shape mismatch (no broadcast)");
    }
    int64_t n = 0;
    if ((st = elem_count(a, &n)) != HBI_OK) {
        return st;
    }
    const float *pa = (const float *)hbi_tensor_cdata(a);
    const float *pb = (const float *)hbi_tensor_cdata(b);
    float *pc = (float *)hbi_tensor_data_mut(c);
    switch (args->params.u.elementwise) {
    case HBI_ELEMENTWISE_ADD:
        for (int64_t i = 0; i < n; ++i) {
            pc[i] = pa[i] + pb[i];
        }
        return HBI_OK;
    case HBI_ELEMENTWISE_MUL:
        for (int64_t i = 0; i < n; ++i) {
            pc[i] = pa[i] * pb[i];
        }
        return HBI_OK;
    case HBI_ELEMENTWISE_KIND_COUNT:
    default:
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "elementwise: bad kind %d",
                            (int)args->params.u.elementwise);
    }
}

/* ── TRANSPOSE ────────────────────────────────────────────────────────────────
 * A materializing swap of two axes (params.transpose.axis_a/axis_b) for a
 * contiguous fp32 tensor of any rank. out is a fresh contiguous tensor whose
 * shape is the input's with the two axes swapped. Implemented by walking the
 * input index space and scattering into the output. */
static hbi_status cpu_transpose_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 1, 1, "transpose");
    if (st != HBI_OK) {
        return st;
    }
    const hbi_tensor *in = args->inputs[0];
    hbi_tensor *out = args->outputs[0];
    if ((st = require_contiguous(in, "transpose.in")) != HBI_OK) {
        return st;
    }
    if ((st = require_contiguous(out, "transpose.out")) != HBI_OK) {
        return st;
    }
    if (hbi_tensor_dtype(in) != HBI_DTYPE_FP32 || hbi_tensor_dtype(out) != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "transpose: fp32 only (reference kernel)");
    }
    const hbi_shape *ishape = hbi_tensor_shape(in);
    uint32_t rank = ishape->rank;
    uint32_t axa = args->params.u.transpose.axis_a;
    uint32_t axb = args->params.u.transpose.axis_b;
    if (axa >= rank || axb >= rank) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "transpose: axis (%u,%u) out of rank %u", axa,
                            axb, rank);
    }
    /* The output shape must be the input shape with axa/axb swapped. */
    hbi_shape want = *ishape;
    int64_t tmp = want.dims[axa];
    want.dims[axa] = want.dims[axb];
    want.dims[axb] = tmp;
    if (!hbi_shape_equal(&want, hbi_tensor_shape(out))) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "transpose: output shape not the swapped input");
    }

    int64_t n = 0;
    if ((st = elem_count(in, &n)) != HBI_OK) {
        return st;
    }
    const float *src = (const float *)hbi_tensor_cdata(in);
    float *dst = (float *)hbi_tensor_data_mut(out);
    const hbi_strides *ostr = hbi_tensor_strides(out); /* contiguous strides */

    /* Odometer over the input's (row-major) index space. For each input index,
     * the destination index is the same with axa/axb swapped; the destination
     * flat offset uses the output's contiguous strides. */
    int64_t idx[HBI_TENSOR_MAX_RANK];
    memset(idx, 0, sizeof(idx));
    for (int64_t flat = 0; flat < n; ++flat) {
        int64_t doff = 0;
        for (uint32_t ax = 0; ax < rank; ++ax) {
            uint32_t sa = ax;
            if (ax == axa) {
                sa = axb;
            } else if (ax == axb) {
                sa = axa;
            }
            doff += idx[sa] * ostr->stride[ax];
        }
        dst[doff] = src[flat];
        /* Increment the row-major odometer over the input dims. */
        for (uint32_t ax = rank; ax > 0; --ax) {
            uint32_t a = ax - 1u;
            if (++idx[a] < ishape->dims[a]) {
                break;
            }
            idx[a] = 0;
        }
    }
    return HBI_OK;
}

/* ── MATMUL ──────────────────────────────────────────────────────────────────
 * C[M,N] = A[M,K] * B[K,N]; 2-D contiguous fp32. Textbook triple loop — the
 * correctness reference, no blocking or SIMD. */
/* Performance: Unoptimized reference implementation */
static hbi_status cpu_matmul_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 2, 1, "matmul");
    if (st != HBI_OK) {
        return st;
    }
    const hbi_tensor *A = args->inputs[0];
    const hbi_tensor *B = args->inputs[1];
    hbi_tensor *C = args->outputs[0];
    if ((st = require_contiguous(A, "matmul.A")) != HBI_OK) {
        return st;
    }
    if ((st = require_contiguous(B, "matmul.B")) != HBI_OK) {
        return st;
    }
    if ((st = require_contiguous(C, "matmul.C")) != HBI_OK) {
        return st;
    }
    if (hbi_tensor_dtype(A) != HBI_DTYPE_FP32 || hbi_tensor_dtype(B) != HBI_DTYPE_FP32 ||
        hbi_tensor_dtype(C) != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "matmul: fp32 only (reference kernel)");
    }
    const hbi_shape *sa = hbi_tensor_shape(A);
    const hbi_shape *sb = hbi_tensor_shape(B);
    const hbi_shape *sc = hbi_tensor_shape(C);
    if (sa->rank != 2 || sb->rank != 2 || sc->rank != 2) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "matmul: operands must be rank-2");
    }
    int64_t M = sa->dims[0];
    int64_t K = sa->dims[1];
    int64_t N = sb->dims[1];
    if (sb->dims[0] != K) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "matmul: inner dims %lld != %lld", (long long)K,
                            (long long)sb->dims[0]);
    }
    if (sc->dims[0] != M || sc->dims[1] != N) {
        return HBI_ERR_SETF(
            HBI_ERR_INVALID_ARG, 0, "matmul: C is [%lld,%lld], expected [%lld,%lld]",
            (long long)sc->dims[0], (long long)sc->dims[1], (long long)M, (long long)N);
    }
    const float *pa = (const float *)hbi_tensor_cdata(A);
    const float *pb = (const float *)hbi_tensor_cdata(B);
    float *pc = (float *)hbi_tensor_data_mut(C);
    for (int64_t i = 0; i < M; ++i) {
        for (int64_t j = 0; j < N; ++j) {
            float acc = 0.0f;
            for (int64_t k = 0; k < K; ++k) {
                acc += pa[i * K + k] * pb[k * N + j];
            }
            pc[i * N + j] = acc;
        }
    }
    return HBI_OK;
}

/* ── SOFTMAX ─────────────────────────────────────────────────────────────────
 * out[i] = exp(in[i] - max(in)) / sum(exp(in[i] - max(in)))
 * 1D or [1,N], fp32. */
static hbi_status cpu_softmax_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 1, 1, "softmax");
    if (st != HBI_OK)
        return st;
    const hbi_tensor *in = args->inputs[0];
    hbi_tensor *out = args->outputs[0];
    if ((st = require_contiguous(in, "softmax.in")) != HBI_OK)
        return st;
    if ((st = require_contiguous(out, "softmax.out")) != HBI_OK)
        return st;
    if (hbi_tensor_dtype(in) != HBI_DTYPE_FP32 || hbi_tensor_dtype(out) != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "softmax: fp32 only (reference kernel)");
    }
    if (!hbi_shape_equal(hbi_tensor_shape(in), hbi_tensor_shape(out))) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "softmax: shape mismatch");
    }
    int64_t n = 0;
    if ((st = elem_count(in, &n)) != HBI_OK)
        return st;
    if (n == 0)
        return HBI_OK;

    const float *pin = (const float *)hbi_tensor_cdata(in);
    float *pout = (float *)hbi_tensor_data_mut(out);

    float max_val = pin[0];
    for (int64_t i = 1; i < n; ++i) {
        if (pin[i] > max_val)
            max_val = pin[i];
    }
    float sum = 0.0f;
    for (int64_t i = 0; i < n; ++i) {
        float e = expf(pin[i] - max_val);
        pout[i] = e;
        sum += e;
    }
    for (int64_t i = 0; i < n; ++i) {
        pout[i] /= sum;
    }
    return HBI_OK;
}

/* ── RMSNORM ─────────────────────────────────────────────────────────────────
 * rms = sqrt(mean(x^2) + eps), out[i] = x[i] / rms * weight[i]; 1D, fp32. */

#ifdef __AVX2__
#include <immintrin.h>
#endif
#ifdef __AVX512F__
#include <immintrin.h>
#endif

#ifdef __AVX2__
static hbi_status cpu_rmsnorm_run_avx2(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)args;
    (void)ws;
    return HBI_ERR_NOT_IMPLEMENTED; // HBI_ERR_NOT_IMPLEMENTED might not exist, let's use HBI_ERR_UNSUPPORTED
}
#endif

static hbi_status cpu_rmsnorm_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
#ifdef __AVX2__
    // Optional dispatch to AVX2
    // return cpu_rmsnorm_run_avx2(args, ws);
#endif

    (void)ws;
    hbi_status st = require_arity(args, 2, 1, "rmsnorm");
    if (st != HBI_OK)
        return st;
    const hbi_tensor *x = args->inputs[0];
    const hbi_tensor *weight = args->inputs[1];
    hbi_tensor *out = args->outputs[0];
    if ((st = require_contiguous(x, "rmsnorm.x")) != HBI_OK)
        return st;
    if ((st = require_contiguous(weight, "rmsnorm.weight")) != HBI_OK)
        return st;
    if ((st = require_contiguous(out, "rmsnorm.out")) != HBI_OK)
        return st;
    if (hbi_tensor_dtype(x) != HBI_DTYPE_FP32 || hbi_tensor_dtype(weight) != HBI_DTYPE_FP32 ||
        hbi_tensor_dtype(out) != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "rmsnorm: fp32 only (reference kernel)");
    }
    if (!hbi_shape_equal(hbi_tensor_shape(x), hbi_tensor_shape(weight)) ||
        !hbi_shape_equal(hbi_tensor_shape(x), hbi_tensor_shape(out))) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "rmsnorm: shape mismatch");
    }
    int64_t n = 0;
    if ((st = elem_count(x, &n)) != HBI_OK)
        return st;
    if (n == 0)
        return HBI_OK;

    const float *px = (const float *)hbi_tensor_cdata(x);
    const float *pw = (const float *)hbi_tensor_cdata(weight);
    float *pout = (float *)hbi_tensor_data_mut(out);

    float sum_sq = 0.0f;
    for (int64_t i = 0; i < n; ++i) {
        sum_sq += px[i] * px[i];
    }
    float mean_sq = sum_sq / (float)n;
    float eps = 1e-5f;
    float rms_inv = 1.0f / sqrtf(mean_sq + eps);
    for (int64_t i = 0; i < n; ++i) {
        pout[i] = (px[i] * rms_inv) * pw[i];
    }
    return HBI_OK;
}

/* ── ROPE ────────────────────────────────────────────────────────────────────
 * x: [seq_len, num_heads, head_dim], out: same shape. fp32.
 * fill_value holds position offset. theta = 10000.0f */
static hbi_status cpu_rope_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 1, 1, "rope");
    if (st != HBI_OK)
        return st;
    const hbi_tensor *in = args->inputs[0];
    hbi_tensor *out = args->outputs[0];
    if ((st = require_contiguous(in, "rope.in")) != HBI_OK)
        return st;
    if ((st = require_contiguous(out, "rope.out")) != HBI_OK)
        return st;
    if (hbi_tensor_dtype(in) != HBI_DTYPE_FP32 || hbi_tensor_dtype(out) != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "rope: fp32 only (reference kernel)");
    }
    const hbi_shape *shape = hbi_tensor_shape(in);
    if (!hbi_shape_equal(shape, hbi_tensor_shape(out))) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "rope: shape mismatch");
    }
    if (shape->rank != 3) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0,
                           "rope: expected rank-3 tensor [seq_len, num_heads, head_dim]");
    }
    int64_t seq_len = shape->dims[0];
    int64_t num_heads = shape->dims[1];
    int64_t head_dim = shape->dims[2];
    if (head_dim % 2 != 0) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "rope: head_dim must be even");
    }

    uint32_t pos_offset = (uint32_t)args->params.u.fill_value;
    const float *pin = (const float *)hbi_tensor_cdata(in);
    float *pout = (float *)hbi_tensor_data_mut(out);
    float theta = 10000.0f;

    for (int64_t s = 0; s < seq_len; ++s) {
        float pos = (float)(s + pos_offset);
        for (int64_t h = 0; h < num_heads; ++h) {
            const float *hin = pin + s * (num_heads * head_dim) + h * head_dim;
            float *hout = pout + s * (num_heads * head_dim) + h * head_dim;
            for (int64_t d = 0; d < head_dim; d += 2) {
                float freq = pos / powf(theta, (float)d / (float)head_dim);
                float cos_f = cosf(freq);
                float sin_f = sinf(freq);
                float x0 = hin[d];
                float x1 = hin[d + 1];
                hout[d] = x0 * cos_f - x1 * sin_f;
                hout[d + 1] = x0 * sin_f + x1 * cos_f;
            }
        }
    }
    return HBI_OK;
}

/* ── ACTIVATION ──────────────────────────────────────────────────────────────
 * out[i] = act(in[i]); gelu/silu/relu; 1D contiguous, fp32. */
static hbi_status cpu_activation_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 1, 1, "activation");
    if (st != HBI_OK)
        return st;
    const hbi_tensor *in = args->inputs[0];
    hbi_tensor *out = args->outputs[0];
    if ((st = require_contiguous(in, "activation.in")) != HBI_OK)
        return st;
    if ((st = require_contiguous(out, "activation.out")) != HBI_OK)
        return st;
    if (hbi_tensor_dtype(in) != HBI_DTYPE_FP32 || hbi_tensor_dtype(out) != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "activation: fp32 only");
    }
    if (!hbi_shape_equal(hbi_tensor_shape(in), hbi_tensor_shape(out))) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "activation: shape mismatch");
    }
    int64_t n = 0;
    if ((st = elem_count(in, &n)) != HBI_OK)
        return st;

    const float *pin = (const float *)hbi_tensor_cdata(in);
    float *pout = (float *)hbi_tensor_data_mut(out);

    switch (args->params.u.activation) {
    case HBI_ACTIVATION_SILU:
        for (int64_t i = 0; i < n; ++i) {
            float x = pin[i];
            pout[i] = x / (1.0f + expf(-x));
        }
        return HBI_OK;
    case HBI_ACTIVATION_GELU:
        for (int64_t i = 0; i < n; ++i) {
            float x = pin[i];
            float cdf = 0.5f * (1.0f + tanhf(0.7978845608f * (x + 0.044715f * x * x * x)));
            pout[i] = x * cdf;
        }
        return HBI_OK;
    case HBI_ACTIVATION_RELU:
        for (int64_t i = 0; i < n; ++i) {
            float x = pin[i];
            pout[i] = x > 0.0f ? x : 0.0f;
        }
        return HBI_OK;
    default:
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "activation: unknown kind %d",
                            (int)args->params.u.activation);
    }
}

/* ── BATCHED_MATMUL ──────────────────────────────────────────────────────────
 * C[B,M,N] = A[B,M,K] * B[B,K,N]; rank-3 contiguous fp32. */
static hbi_status cpu_batched_matmul_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 2, 1, "batched_matmul");
    if (st != HBI_OK)
        return st;
    const hbi_tensor *A = args->inputs[0];
    const hbi_tensor *B = args->inputs[1];
    hbi_tensor *C = args->outputs[0];
    if ((st = require_contiguous(A, "batched_matmul.A")) != HBI_OK)
        return st;
    if ((st = require_contiguous(B, "batched_matmul.B")) != HBI_OK)
        return st;
    if ((st = require_contiguous(C, "batched_matmul.C")) != HBI_OK)
        return st;
    if (hbi_tensor_dtype(A) != HBI_DTYPE_FP32 || hbi_tensor_dtype(B) != HBI_DTYPE_FP32 ||
        hbi_tensor_dtype(C) != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "batched_matmul: fp32 only");
    }
    const hbi_shape *sa = hbi_tensor_shape(A);
    const hbi_shape *sb = hbi_tensor_shape(B);
    const hbi_shape *sc = hbi_tensor_shape(C);
    if (sa->rank != 3 || sb->rank != 3 || sc->rank != 3) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "batched_matmul: operands must be rank-3");
    }
    int64_t batch = sa->dims[0];
    if (sb->dims[0] != batch || sc->dims[0] != batch) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "batched_matmul: batch size mismatch");
    }
    int64_t M = sa->dims[1];
    int64_t K = sa->dims[2];
    int64_t N = sb->dims[2];
    if (sb->dims[1] != K) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "batched_matmul: inner dim mismatch");
    }
    if (sc->dims[1] != M || sc->dims[2] != N) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "batched_matmul: output shape mismatch");
    }

    const float *pa = (const float *)hbi_tensor_cdata(A);
    const float *pb = (const float *)hbi_tensor_cdata(B);
    float *pc = (float *)hbi_tensor_data_mut(C);

    for (int64_t b = 0; b < batch; ++b) {
        const float *a_batch = pa + b * M * K;
        const float *b_batch = pb + b * K * N;
        float *c_batch = pc + b * M * N;
        for (int64_t i = 0; i < M; ++i) {
            for (int64_t j = 0; j < N; ++j) {
                float acc = 0.0f;
                for (int64_t k = 0; k < K; ++k) {
                    acc += a_batch[i * K + k] * b_batch[k * N + j];
                }
                c_batch[i * N + j] = acc;
            }
        }
    }
    return HBI_OK;
}

/* ── REDUCE ──────────────────────────────────────────────────────────────────
 * out[0] = reduce(in); flat contiguous fp32. */
static hbi_status cpu_reduce_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)ws;
    hbi_status st = require_arity(args, 1, 1, "reduce");
    if (st != HBI_OK)
        return st;
    const hbi_tensor *in = args->inputs[0];
    hbi_tensor *out = args->outputs[0];
    if ((st = require_contiguous(in, "reduce.in")) != HBI_OK)
        return st;
    if ((st = require_contiguous(out, "reduce.out")) != HBI_OK)
        return st;
    if (hbi_tensor_dtype(in) != HBI_DTYPE_FP32 || hbi_tensor_dtype(out) != HBI_DTYPE_FP32) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "reduce: fp32 only");
    }
    int64_t n = 0;
    if ((st = elem_count(in, &n)) != HBI_OK)
        return st;
    int64_t out_n = 0;
    if ((st = elem_count(out, &out_n)) != HBI_OK)
        return st;
    if (out_n != 1) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "reduce: output must be scalar (1 element)");
    }

    const float *pin = (const float *)hbi_tensor_cdata(in);
    float *pout = (float *)hbi_tensor_data_mut(out);
    if (n == 0) {
        pout[0] = 0.0f;
        return HBI_OK;
    }

    switch (args->params.u.reduce) {
    case HBI_REDUCE_SUM: {
        float sum = 0.0f;
        for (int64_t i = 0; i < n; ++i)
            sum += pin[i];
        pout[0] = sum;
        return HBI_OK;
    }
    case HBI_REDUCE_MAX: {
        float max_val = pin[0];
        for (int64_t i = 1; i < n; ++i) {
            if (pin[i] > max_val)
                max_val = pin[i];
        }
        pout[0] = max_val;
        return HBI_OK;
    }
    default:
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "reduce: unknown kind %d",
                            (int)args->params.u.reduce);
    }
}

/* ── Descriptors + registration ──────────────────────────────────────────────
 * Dtype sets are static (the registry stores the pointer, not a copy). */
static const hbi_dtype k_byte_dtypes[] = {HBI_DTYPE_FP32, HBI_DTYPE_INT8};
static const hbi_dtype k_fp32_only[] = {HBI_DTYPE_FP32};

static const hbi_kernel k_cpu_kernels[] = {
    {
        .op = HBI_KERNEL_OP_COPY,
        .name = "cpu.copy",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_byte_dtypes,
        .num_dtypes = 2u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_copy_run,
    },
    {
        .op = HBI_KERNEL_OP_FILL,
        .name = "cpu.fill",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_byte_dtypes,
        .num_dtypes = 2u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_fill_run,
    },
    {
        .op = HBI_KERNEL_OP_CAST,
        .name = "cpu.cast",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_byte_dtypes,
        .num_dtypes = 2u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_cast_run,
    },
    {
        .op = HBI_KERNEL_OP_ELEMENTWISE,
        .name = "cpu.elementwise.fp32",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_fp32_only,
        .num_dtypes = 1u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_elementwise_run,
    },
    {
        .op = HBI_KERNEL_OP_TRANSPOSE,
        .name = "cpu.transpose.fp32",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_fp32_only,
        .num_dtypes = 1u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_transpose_run,
    },
    {
        .op = HBI_KERNEL_OP_MATMUL,
        .name = "cpu.matmul.fp32",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_fp32_only,
        .num_dtypes = 1u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_matmul_run,
    },
    {
        .op = HBI_KERNEL_OP_SOFTMAX,
        .name = "cpu.softmax.fp32",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_fp32_only,
        .num_dtypes = 1u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_softmax_run,
    },
    {
        .op = HBI_KERNEL_OP_RMSNORM,
        .name = "cpu.rmsnorm.fp32",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_fp32_only,
        .num_dtypes = 1u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_rmsnorm_run,
    },
    {
        .op = HBI_KERNEL_OP_ROPE,
        .name = "cpu.rope.fp32",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_fp32_only,
        .num_dtypes = 1u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_rope_run,
    },
    {
        .op = HBI_KERNEL_OP_ACTIVATION,
        .name = "cpu.activation.fp32",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_fp32_only,
        .num_dtypes = 1u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_activation_run,
    },
    {
        .op = HBI_KERNEL_OP_BATCHED_MATMUL,
        .name = "cpu.batched_matmul.fp32",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_fp32_only,
        .num_dtypes = 1u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_batched_matmul_run,
    },
    {
        .op = HBI_KERNEL_OP_REDUCE,
        .name = "cpu.reduce.fp32",
        .device = HBI_TENSOR_DEVICE_CPU,
        .supported_dtypes = k_fp32_only,
        .num_dtypes = 1u,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
        .workspace_size = NULL,
        .run = cpu_reduce_run,
    },
};

hbi_status hb_backend_cpu_register_kernels(void) {
    for (size_t i = 0; i < HB_ARRAY_LEN(k_cpu_kernels); ++i) {
        hbi_status st = hbi_kernel_register(&k_cpu_kernels[i]);
        if (st != HBI_OK) {
            return st;
        }
    }
    return HBI_OK;
}
