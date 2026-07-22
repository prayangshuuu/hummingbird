/* bench_planner.c — Micro-benchmark for the memory planner */
#include "graph/graph.h"
#include "planner/planner.h"
#include "profiler/profiler.h"
#include <stdio.h>
#include <stdlib.h>

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg);                         \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void bench_planner_deep_linear(int num_nodes) {
    hbi_graph_builder *b = NULL;
    ASSERT(hbi_graph_builder_create(&b) == HBI_OK, "create builder");

    int64_t dims[] = {4096, 4096}; /* 16M elements * 4 bytes = 64MB per tensor */
    hbi_shape s;
    ASSERT(hbi_shape_init(&s, dims, 2) == HBI_OK, "shape init");

    uint32_t curr_id;
    ASSERT(hbi_graph_add_input(b, "in", &s, HBI_DTYPE_FP32, &curr_id) == HBI_OK, "add input");

    for (int i = 0; i < num_nodes; ++i) {
        uint32_t next_id;
        char name[64];
        snprintf(name, sizeof(name), "layer_%d", i);
        ASSERT(hbi_graph_add_node(b, name, HBI_KERNEL_OP_COPY, NULL, &curr_id, 1, &next_id, 1) ==
                   HBI_OK,
               "add node");
        curr_id = next_id;
    }

    hbi_graph *g = NULL;
    ASSERT(hbi_graph_build(b, &g) == HBI_OK, "build graph");

    hbi_memory_planner *planner = NULL;
    ASSERT(hbi_memory_planner_create(g, &planner) == HBI_OK, "create planner");

    hbi_memory_plan *plan = NULL;
    {
        HBI_PROF_SCOPE(prof_plan, "planner_deep_linear");
        ASSERT(hbi_memory_planner_plan(planner, &plan) == HBI_OK, "generate plan");
    }

    hbi_memory_statistics stats;
    ASSERT(hbi_memory_plan_get_statistics(plan, &stats) == HBI_OK, "get stats");

    printf("--- Benchmark: %d Node Linear Graph ---\n", num_nodes);
    printf("Naive Memory:      %.2f MB\n", (double)stats.naive_memory_bytes / 1024.0 / 1024.0);
    printf("Optimal Memory:    %.2f MB\n", (double)stats.temporary_memory_bytes / 1024.0 / 1024.0);
    printf("Compression Ratio: %.2fx\n\n",
           (double)stats.naive_memory_bytes / (double)stats.temporary_memory_bytes);

    hbi_memory_planner_destroy(planner);
    hbi_graph_destroy(g);
}

int main(void) {
    printf("Hummingbird Memory Planner Benchmarks\n");
    printf("=====================================\n\n");

    bench_planner_deep_linear(100);
    bench_planner_deep_linear(1000);

    return 0;
}
