/* bench_graph.c — micro-benchmark for graph construction and execution.
 *
 * Measures overhead of building, verifying, and executing graphs (without the
 * heavy lifting of actual math - dispatch cost vs raw kernel cost).
 */
#include "executor/executor.h"
#include "graph/graph.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>

/* This benchmark is standalone for now. It simply constructs a large-ish chain of operations
 * and benchmarks the dispatch execution time. We can just use the CPU backend's reference kernels
 * if they are registered.
 * Note: we must link the CPU backend and call its registration.
 */
extern hbi_status hb_backend_cpu_register_kernels(void);

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if ((expr) != HBI_OK) {                                                                    \
            fprintf(stderr, "FAIL: %s\n", #expr);                                                  \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void) {
    CHECK(hb_backend_cpu_register_kernels());

    hbi_graph_builder *b = NULL;
    CHECK(hbi_graph_builder_create(&b));

    int64_t dims[] = {128, 128};
    hbi_shape s;
    CHECK(hbi_shape_init(&s, dims, 2));

    uint32_t in_id;
    CHECK(hbi_graph_add_input(b, "input", &s, HBI_DTYPE_FP32, &in_id));

    uint32_t current = in_id;
    int num_ops = 1000; /* a long chain */
    for (int i = 0; i < num_ops; ++i) {
        uint32_t out_id;
        hbi_kernel_params p = {0};
        p.u.transpose.axis_a = 0;
        p.u.transpose.axis_b = 1;

        char name[32];
        snprintf(name, sizeof(name), "op_%d", i);
        CHECK(hbi_graph_add_node(b, name, HBI_KERNEL_OP_TRANSPOSE, &p, &current, 1, &out_id, 1));
        current = out_id;
    }

    uint64_t t0 = hbi_time_wall_ns();
    hbi_graph *g = NULL;
    CHECK(hbi_graph_build(b, &g));
    uint64_t t1 = hbi_time_wall_ns();

    printf("graph_build_1k_nodes_ns: %llu\n", (unsigned long long)(t1 - t0));

    t0 = hbi_time_wall_ns();
    hbi_executor *ex = NULL;
    CHECK(hbi_executor_create(g, &ex));
    t1 = hbi_time_wall_ns();
    printf("executor_create_1k_nodes_ns: %llu\n", (unsigned long long)(t1 - t0));

    hbi_exec_context *ctx = NULL;
    CHECK(hbi_exec_context_create(g, hbi_allocator_system(), &ctx));

    hbi_tensor in_t;
    CHECK(hbi_tensor_alloc(&in_t, HBI_DTYPE_FP32, &s));
    CHECK(hbi_exec_context_bind(ctx, in_id, &in_t));

    CHECK(hbi_exec_context_allocate_internals(ctx));

    /* Warmup */
    CHECK(hbi_executor_run(ex, ctx));

    /* Benchmark */
    uint64_t runs = 100;
    t0 = hbi_time_wall_ns();
    for (uint64_t i = 0; i < runs; ++i) {
        CHECK(hbi_executor_run(ex, ctx));
    }
    t1 = hbi_time_wall_ns();

    printf("executor_run_1k_nodes_ns_per_run: %llu\n", (unsigned long long)((t1 - t0) / runs));

    hbi_tensor_destroy(&in_t);
    hbi_exec_context_destroy(ctx);
    hbi_executor_destroy(ex);
    hbi_graph_destroy(g);

    return 0;
}
