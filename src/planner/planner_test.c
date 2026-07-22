/* planner_test.c — Unit tests for the memory planner */
#include "planner/planner.h"
#include <stdio.h>
#include <stdlib.h>

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg);                         \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_OK(st, msg) ASSERT((st) == HBI_OK, msg)

static void test_planner_linear(void) {
    hbi_graph_builder *b = NULL;
    ASSERT_OK(hbi_graph_builder_create(&b), "create builder");

    int64_t dims[] = {128, 128};
    hbi_shape s;
    ASSERT_OK(hbi_shape_init(&s, dims, 2), "shape init");

    /* 128x128 fp32 = 65536 bytes */
    size_t expected_size = 65536;

    uint32_t in_id;
    ASSERT_OK(hbi_graph_add_input(b, "in", &s, HBI_DTYPE_FP32, &in_id), "add input");

    uint32_t curr_id = in_id;

    /* Chain of 4 element-wise ops (or identity).
     * Op 0: out1
     * Op 1: out2
     * Op 2: out3
     * Op 3: out4
     * Tensors out1 and out3 can share memory. Tensors out2 and out4 can share memory.
     * Peak memory for the temporaries should be 2 * expected_size.
     */
    for (int i = 0; i < 4; ++i) {
        uint32_t next_id;
        char name[32];
        snprintf(name, sizeof(name), "op_%d", i);

        ASSERT_OK(hbi_graph_add_node(b, name, HBI_KERNEL_OP_COPY, NULL, &curr_id, 1, &next_id, 1),
                  "add node");
        curr_id = next_id;
    }

    hbi_graph *g = NULL;
    ASSERT_OK(hbi_graph_build(b, &g), "build graph");

    hbi_memory_planner *planner = NULL;
    ASSERT_OK(hbi_memory_planner_create(g, &planner), "create planner");

    hbi_memory_plan *plan = NULL;
    ASSERT_OK(hbi_memory_planner_plan(planner, &plan), "plan");

    hbi_memory_statistics stats;
    ASSERT_OK(hbi_memory_plan_get_statistics(plan, &stats), "get stats");

    /* Input is external (persistent).
     * We have 4 intermediate tensors. Naively: 4 * 65536 = 262144 bytes.
     * With reuse, we only need 2 * 65536 = 131072 bytes.
     */
    ASSERT(stats.naive_memory_bytes == 4 * expected_size, "naive memory");
    ASSERT(stats.temporary_memory_bytes == 2 * expected_size, "optimal temporary memory");

    hbi_memory_planner_destroy(planner);
    hbi_graph_destroy(g);
}

int main(void) {
    ASSERT_OK(hbi_planner_selftest(), "selftest");

    test_planner_linear();

    printf("[ok] planner\n");
    return 0;
}
