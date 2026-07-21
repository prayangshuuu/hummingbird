/* bench_alloc.c — micro-benchmark for the system allocator (memory module).
 *
 * Measures alloc/free throughput at a few sizes and reports nanoseconds per
 * operation plus operations per second. This is an INTERNAL benchmark: it links
 * the internal hb_memory static library directly (not the public ABI), because
 * the allocator is an internal subsystem with no public surface yet. That is a
 * deliberate, documented exception to the "benchmarks link the public library"
 * guideline (see PROJECT_CONTEXT DD-020/benchmarks note) — internal foundation
 * pieces need internal harnesses.
 *
 * Not a correctness test (that is memory_test.c); numbers are indicative and
 * feed PROJECT_CONTEXT §9 only when run on recorded hardware.
 */
#include "memory/memory.h"
#include "platform/platform.h"

#include <stdio.h>

enum { WARMUP = 10000, ITERS = 200000 };

static double bench_size(hbi_allocator *a, size_t size, size_t alignment) {
    /* Warm up so the allocator's first-touch costs do not skew the timing. */
    for (int i = 0; i < WARMUP; ++i) {
        void *p = hbi_alloc(a, size, alignment, HBI_MEM_GENERAL);
        hbi_free(a, p);
    }

    uint64_t start = hbi_time_monotonic_ns();
    for (int i = 0; i < ITERS; ++i) {
        void *p = hbi_alloc(a, size, alignment, HBI_MEM_GENERAL);
        /* Touch one byte so the compiler cannot elide the allocation. */
        if (p != NULL) {
            ((volatile unsigned char *)p)[0] = (unsigned char)i;
        }
        hbi_free(a, p);
    }
    uint64_t elapsed = hbi_time_monotonic_ns() - start;

    /* Each iteration is one alloc + one free = 2 ops. */
    double ns_per_op = (double)elapsed / (double)(ITERS * 2);
    return ns_per_op;
}

int main(void) {
    hbi_allocator *a = hbi_allocator_system();
    const size_t sizes[] = {16, 64, 256, 4096, 65536};
    const size_t aligns[] = {0, 0, 64, 64, 4096};

    printf("system allocator micro-benchmark (%d iters/size)\n", ITERS);
    printf("%10s %10s %14s %16s\n", "size", "align", "ns/op", "ops/sec");
    for (size_t i = 0; i < HB_ARRAY_LEN(sizes); ++i) {
        double ns = bench_size(a, sizes[i], aligns[i]);
        double ops_per_sec = ns > 0.0 ? 1e9 / ns : 0.0;
        printf("%10zu %10zu %14.2f %16.0f\n", sizes[i], aligns[i], ns, ops_per_sec);
    }
    return 0;
}
