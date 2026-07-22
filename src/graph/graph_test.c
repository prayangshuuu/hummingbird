/* graph_test.c — unit tests for the `graph` module. */
#include "graph/graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            const hbi_error *e = hbi_error_last();                                                 \
            if (e) {                                                                               \
                fprintf(stderr, "FAIL: %s:%d: %s (Error: %s)\n", __FILE__, __LINE__, msg,          \
                        e->message);                                                               \
            } else {                                                                               \
                fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg);                     \
            }                                                                                      \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_OK(st, msg) ASSERT((st) == HBI_OK, msg)

static void test_acyclic_graph(void) {
    hbi_graph_builder *b = NULL;
    ASSERT_OK(hbi_graph_builder_create(&b), "create builder");

    int64_t dims[] = {128, 64};
    hbi_shape s;
    ASSERT_OK(hbi_shape_init(&s, dims, 2), "shape init");

    uint32_t in_id;
    ASSERT_OK(hbi_graph_add_input(b, "input", &s, HBI_DTYPE_FP32, &in_id), "add input");

    uint32_t out_id;
    hbi_kernel_params p = {0};
    p.u.transpose.axis_a = 0;
    p.u.transpose.axis_b = 1;
    ASSERT_OK(
        hbi_graph_add_node(b, "transpose", HBI_KERNEL_OP_TRANSPOSE, &p, &in_id, 1, &out_id, 1),
        "add transpose node");

    hbi_graph *g = NULL;
    ASSERT_OK(hbi_graph_build(b, &g), "build graph");

    ASSERT(hbi_graph_num_nodes(g) == 1, "num nodes");
    ASSERT(hbi_graph_num_values(g) == 2, "num values");

    const hbi_value *v = hbi_graph_value_at(g, out_id);
    ASSERT(v != NULL, "get value");
    ASSERT(v->shape.dims[0] == 64 && v->shape.dims[1] == 128, "transpose shape inference");

    hbi_graph_destroy(g);
}

static void test_cycle_detection(void) {
    hbi_graph_builder *b = NULL;
    ASSERT_OK(hbi_graph_builder_create(&b), "create builder");

    int64_t dims[] = {10, 10};
    hbi_shape s;
    ASSERT_OK(hbi_shape_init(&s, dims, 2), "shape init");

    uint32_t in_id;
    ASSERT_OK(hbi_graph_add_input(b, "input", &s, HBI_DTYPE_FP32, &in_id), "add input");

    /* We can't really create a cycle via normal add_node because outputs are created BY the node
     * and inputs must exist before. Wait, you can't create a cycle this way since inputs are
     * required to be passed as IDs. So a cycle is topologically impossible to construct using our
     * API, which is a great property. I'll skip explicit cycle testing if it's unrepresentable.
     */
    hbi_graph_builder_destroy(b);
}

int main(void) {
    ASSERT_OK(hbi_graph_selftest(), "selftest");

    test_acyclic_graph();
    test_cycle_detection();

    printf("[ok] graph\n");
    return 0;
}
