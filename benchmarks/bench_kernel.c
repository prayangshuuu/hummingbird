/* bench_kernel.c — micro-benchmarks for the kernel runtime (RFC-003, DD-025).
 *
 * Establishes the future-optimization baselines RFC-003 asks for: dispatch
 * overhead, the reference matmul, memory copy, transpose, and workspace
 * allocation. This is an INTERNAL benchmark: it links the internal hb_kernel +
 * hb_backend_cpu static libraries directly (not the public ABI), for the same
 * reason as bench_alloc.c / bench_threadpool.c — the kernel runtime has no public
 * surface yet. That is the documented exception to the "benchmarks link the
 * public library" guideline (see PROJECT_CONTEXT DD-025/benchmarks note).
 *
 * Not a correctness test (that is kernel_test.c / backend_cpu_kernels_test.c);
 * numbers are indicative and feed PROJECT_CONTEXT §9 only when run on recorded
 * hardware. The kernels are the SCALAR reference — these numbers are the floor
 * SIMD/IDOT (M2) will be measured against, not a target.
 */
#include "backend_cpu_kernels.h"
#include "kernel/kernel.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>

/* Fill a fresh contiguous fp32 tensor of `n` elements with a cheap ramp so the
 * compiler cannot treat the buffer as dead. Returns HBI_OK / prints on failure. */
static hbi_status make_vec(hbi_tensor *t, int64_t n) {
    hbi_shape s;
    int64_t dims[1] = {n};
    hbi_status st = hbi_shape_init(&s, dims, 1);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_tensor_alloc(t, HBI_DTYPE_FP32, &s);
    if (st != HBI_OK) {
        return st;
    }
    float *p = (float *)t->data;
    for (int64_t i = 0; i < n; ++i) {
        p[i] = (float)(i & 1023);
    }
    return HBI_OK;
}

static hbi_status make_mat(hbi_tensor *t, int64_t rows, int64_t cols) {
    hbi_shape s;
    int64_t dims[2] = {rows, cols};
    hbi_status st = hbi_shape_init(&s, dims, 2);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_tensor_alloc(t, HBI_DTYPE_FP32, &s);
    if (st != HBI_OK) {
        return st;
    }
    float *p = (float *)t->data;
    for (int64_t i = 0; i < rows * cols; ++i) {
        p[i] = (float)(i & 255) * 0.01f;
    }
    return HBI_OK;
}

/* ── Dispatch overhead ────────────────────────────────────────────────────────
 * Time the resolve+run path on the cheapest possible op (an 8-element copy) so
 * the number reflects the dispatch machinery — key build, registry scan, dtype
 * inference, workspace check — not the kernel's compute. */
static double bench_dispatch(int iters) {
    hbi_tensor in, out;
    if (make_vec(&in, 8) != HBI_OK || make_vec(&out, 8) != HBI_OK) {
        return 0.0;
    }
    hbi_kernel_args args;
    (void)hbi_kernel_args_init(&args);
    args.inputs[0] = &in;
    args.num_inputs = 1;
    args.outputs[0] = &out;
    args.num_outputs = 1;

    uint64_t start = hbi_time_monotonic_ns();
    for (int i = 0; i < iters; ++i) {
        (void)hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &args, NULL);
    }
    uint64_t elapsed = hbi_time_monotonic_ns() - start;

    hbi_tensor_destroy(&in);
    hbi_tensor_destroy(&out);
    return (double)elapsed / (double)iters; /* ns per dispatch */
}

/* ── Resolve-only overhead ────────────────────────────────────────────────────
 * The registry lookup in isolation (what the executor pays once and then caches).
 * Confirms that caching the descriptor is worth it vs resolving per call. */
static double bench_resolve(int iters) {
    hbi_kernel_key key = {
        .op = HBI_KERNEL_OP_MATMUL,
        .device = HBI_TENSOR_DEVICE_CPU,
        .dtype = HBI_DTYPE_FP32,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
    };
    const hbi_kernel *k = NULL;
    uint64_t start = hbi_time_monotonic_ns();
    for (int i = 0; i < iters; ++i) {
        (void)hbi_kernel_resolve(&key, &k);
    }
    uint64_t elapsed = hbi_time_monotonic_ns() - start;
    return (double)elapsed / (double)iters; /* ns per resolve */
}

/* ── Reference matmul ─────────────────────────────────────────────────────────
 * Square NxN fp32 matmul through dispatch. Reports ns and derived GFLOP/s
 * (2*N^3 flops). The scalar triple-loop floor for M2 to beat. */
static void bench_matmul(int64_t N, int reps) {
    hbi_tensor a, b, c;
    if (make_mat(&a, N, N) != HBI_OK || make_mat(&b, N, N) != HBI_OK ||
        make_mat(&c, N, N) != HBI_OK) {
        return;
    }
    hbi_kernel_args args;
    (void)hbi_kernel_args_init(&args);
    args.inputs[0] = &a;
    args.inputs[1] = &b;
    args.num_inputs = 2;
    args.outputs[0] = &c;
    args.num_outputs = 1;

    uint64_t start = hbi_time_monotonic_ns();
    for (int r = 0; r < reps; ++r) {
        (void)hbi_kernel_dispatch(HBI_KERNEL_OP_MATMUL, HBI_TENSOR_DEVICE_CPU, &args, NULL);
    }
    uint64_t elapsed = hbi_time_monotonic_ns() - start;

    double ns = (double)elapsed / (double)reps;
    double flops = 2.0 * (double)N * (double)N * (double)N;
    double gflops = ns > 0.0 ? flops / ns : 0.0; /* flops/ns == GFLOP/s */
    printf("%10s %6lld %14.1f %16.3f\n", "matmul", (long long)N, ns, gflops);

    hbi_tensor_destroy(&a);
    hbi_tensor_destroy(&b);
    hbi_tensor_destroy(&c);
}

/* ── Memory copy ──────────────────────────────────────────────────────────────
 * A large 1-D copy through dispatch; reports ns and GB/s (bytes moved = 2*n*4:
 * read + write). */
static void bench_copy(int64_t n, int reps) {
    hbi_tensor in, out;
    if (make_vec(&in, n) != HBI_OK || make_vec(&out, n) != HBI_OK) {
        return;
    }
    hbi_kernel_args args;
    (void)hbi_kernel_args_init(&args);
    args.inputs[0] = &in;
    args.num_inputs = 1;
    args.outputs[0] = &out;
    args.num_outputs = 1;

    uint64_t start = hbi_time_monotonic_ns();
    for (int r = 0; r < reps; ++r) {
        (void)hbi_kernel_dispatch(HBI_KERNEL_OP_COPY, HBI_TENSOR_DEVICE_CPU, &args, NULL);
    }
    uint64_t elapsed = hbi_time_monotonic_ns() - start;

    double ns = (double)elapsed / (double)reps;
    double bytes = 2.0 * (double)n * (double)sizeof(float);
    double gbs = ns > 0.0 ? bytes / ns : 0.0; /* bytes/ns == GB/s */
    printf("%10s %6lld %14.1f %16.3f\n", "copy", (long long)n, ns, gbs);

    hbi_tensor_destroy(&in);
    hbi_tensor_destroy(&out);
}

/* ── Transpose ────────────────────────────────────────────────────────────────
 * A materializing NxN fp32 transpose through dispatch; reports ns and the
 * effective GB/s over the moved data (read + write). */
static void bench_transpose(int64_t N, int reps) {
    hbi_tensor in, out;
    if (make_mat(&in, N, N) != HBI_OK || make_mat(&out, N, N) != HBI_OK) {
        return;
    }
    hbi_kernel_args args;
    (void)hbi_kernel_args_init(&args);
    args.inputs[0] = &in;
    args.num_inputs = 1;
    args.outputs[0] = &out;
    args.num_outputs = 1;
    args.params.u.transpose.axis_a = 0;
    args.params.u.transpose.axis_b = 1;

    uint64_t start = hbi_time_monotonic_ns();
    for (int r = 0; r < reps; ++r) {
        (void)hbi_kernel_dispatch(HBI_KERNEL_OP_TRANSPOSE, HBI_TENSOR_DEVICE_CPU, &args, NULL);
    }
    uint64_t elapsed = hbi_time_monotonic_ns() - start;

    double ns = (double)elapsed / (double)reps;
    double bytes = 2.0 * (double)N * (double)N * (double)sizeof(float);
    double gbs = ns > 0.0 ? bytes / ns : 0.0;
    printf("%10s %6lld %14.1f %16.3f\n", "transpose", (long long)N, ns, gbs);

    hbi_tensor_destroy(&in);
    hbi_tensor_destroy(&out);
}

/* ── Workspace reserve/reset ──────────────────────────────────────────────────
 * The steady-state cost of a warmed workspace (reserve within capacity is a
 * no-op) vs a cold reserve (allocates). Reports ns per reserve for a request
 * that always fits (the hot-path case). */
static double bench_workspace_warm(int iters) {
    hbi_kernel_workspace ws;
    if (hbi_kernel_workspace_init(&ws, NULL) != HBI_OK) {
        return 0.0;
    }
    if (hbi_kernel_workspace_reserve(&ws, 4096, 64) != HBI_OK) {
        hbi_kernel_workspace_destroy(&ws);
        return 0.0;
    }
    uint64_t start = hbi_time_monotonic_ns();
    for (int i = 0; i < iters; ++i) {
        /* Always fits → must NOT allocate; this is the warm-path invariant. */
        (void)hbi_kernel_workspace_reserve(&ws, 4096, 64);
    }
    uint64_t elapsed = hbi_time_monotonic_ns() - start;
    hbi_kernel_workspace_destroy(&ws);
    return (double)elapsed / (double)iters; /* ns per warm reserve */
}

int main(void) {
    if (hb_backend_cpu_register_kernels() != HBI_OK) {
        fprintf(stderr, "bench_kernel: failed to register CPU kernels\n");
        return 1;
    }

    printf("kernel runtime micro-benchmarks (scalar reference — the M2 floor)\n\n");

    printf("dispatch / resolve overhead (ns per call)\n");
    printf("%22s %14.2f\n", "dispatch (copy8)", bench_dispatch(1000000));
    printf("%22s %14.2f\n", "resolve (matmul)", bench_resolve(1000000));
    printf("%22s %14.2f\n\n", "workspace warm reserve", bench_workspace_warm(1000000));

    printf("op baselines\n");
    printf("%10s %6s %14s %16s\n", "op", "N", "ns/call", "GFLOP/s | GB/s");
    bench_matmul(64, 200);
    bench_matmul(128, 50);
    bench_matmul(256, 10);
    bench_copy(1 << 20, 200); /* 1M elems */
    bench_copy(1 << 22, 50);  /* 4M elems */
    bench_transpose(512, 50);
    bench_transpose(1024, 10);

    hbi_kernel_registry_clear();
    return 0;
}
