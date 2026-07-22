/* scheduler_test.c — tests for the Scheduler (RFC-007) */
#include "graph/graph.h"
#include "scheduler/scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HBI_CHECK(cond)                                                                            \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define HBI_CHECK_EQ_INT(a, b) HBI_CHECK((a) == (b))

static void test_scheduler_creation(void) {
    hbi_scheduler *sch = NULL;
    HBI_CHECK_EQ_INT(hbi_scheduler_create(NULL, NULL), HBI_ERR_INVALID_ARG);
    HBI_CHECK_EQ_INT(hbi_scheduler_create(NULL, &sch), HBI_OK);
    HBI_CHECK(sch != NULL);
    hbi_scheduler_destroy(sch);
}

static void test_scheduler_plan_generation(void) {
    hbi_scheduler *sch = NULL;
    HBI_CHECK_EQ_INT(hbi_scheduler_create(NULL, &sch), HBI_OK);

    hbi_graph_builder *builder = NULL;
    HBI_CHECK_EQ_INT(hbi_graph_builder_create(&builder), HBI_OK);

    hbi_shape shape = {1, {10}};
    uint32_t v1, v2;
    HBI_CHECK_EQ_INT(hbi_graph_add_input(builder, "in1", &shape, HBI_DTYPE_FP32, &v1), HBI_OK);
    HBI_CHECK_EQ_INT(hbi_graph_add_input(builder, "in2", &shape, HBI_DTYPE_FP32, &v2), HBI_OK);

    uint32_t inputs[2] = {v1, v2};
    uint32_t out1, out2;

    hbi_kernel_params params = {0};
    params.u.elementwise = HBI_ELEMENTWISE_ADD;

    /* Add a couple of dummy operations */
    HBI_CHECK_EQ_INT(hbi_graph_add_node(builder, "add1", HBI_KERNEL_OP_ELEMENTWISE, &params, inputs,
                                        2, &out1, 1),
                     HBI_OK);

    uint32_t inputs2[2] = {out1, v1};
    HBI_CHECK_EQ_INT(hbi_graph_add_node(builder, "add2", HBI_KERNEL_OP_ELEMENTWISE, &params,
                                        inputs2, 2, &out2, 1),
                     HBI_OK);

    hbi_graph *graph = NULL;
    HBI_CHECK_EQ_INT(hbi_graph_build(builder, &graph), HBI_OK);

    hbi_execution_plan *plan = NULL;
    HBI_CHECK_EQ_INT(hbi_scheduler_create_plan(sch, graph, &plan), HBI_OK);
    HBI_CHECK(plan != NULL);

    HBI_CHECK_EQ_INT(plan->num_tasks, 2);
    HBI_CHECK_EQ_INT(plan->num_stages, 2);

    HBI_CHECK(plan->tasks[0].task_id == 0);
    HBI_CHECK(plan->tasks[0].required_device == HBI_DEVICE_TYPE_CPU);
    HBI_CHECK(plan->tasks[0].num_dependencies == 0);

    HBI_CHECK(plan->tasks[1].task_id == 1);
    HBI_CHECK(plan->tasks[1].num_dependencies == 1);
    HBI_CHECK(plan->tasks[1].dependencies[0] == 0);

    HBI_CHECK(plan->stages[0].num_tasks == 1);
    HBI_CHECK(plan->stages[0].tasks[0] == &plan->tasks[0]);
    HBI_CHECK(plan->stages[0].completion_barrier.type == HBI_SYNC_BARRIER);

    HBI_CHECK(plan->stats.total_tasks == 2);
    HBI_CHECK(plan->stats.num_stages == 2);

    hbi_execution_plan_destroy(sch, plan);
    hbi_graph_destroy(graph);
    hbi_scheduler_destroy(sch);
}

static void test_scheduler_branching_graph(void) {
    hbi_scheduler *sch = NULL;
    HBI_CHECK_EQ_INT(hbi_scheduler_create(NULL, &sch), HBI_OK);

    hbi_graph_builder *builder = NULL;
    HBI_CHECK_EQ_INT(hbi_graph_builder_create(&builder), HBI_OK);

    hbi_shape shape = {1, {10}};
    uint32_t v1;
    HBI_CHECK_EQ_INT(hbi_graph_add_input(builder, "in1", &shape, HBI_DTYPE_FP32, &v1), HBI_OK);

    hbi_kernel_params params = {0};
    params.u.elementwise = HBI_ELEMENTWISE_ADD;

    /* A -> B and A -> C, then B+C -> D */
    uint32_t outA, outB, outC, outD;

    uint32_t inA[2] = {v1, v1};
    HBI_CHECK_EQ_INT(
        hbi_graph_add_node(builder, "A", HBI_KERNEL_OP_ELEMENTWISE, &params, inA, 2, &outA, 1),
        HBI_OK);

    uint32_t inB[2] = {outA, outA};
    HBI_CHECK_EQ_INT(
        hbi_graph_add_node(builder, "B", HBI_KERNEL_OP_ELEMENTWISE, &params, inB, 2, &outB, 1),
        HBI_OK);

    uint32_t inC[2] = {outA, outA};
    HBI_CHECK_EQ_INT(
        hbi_graph_add_node(builder, "C", HBI_KERNEL_OP_ELEMENTWISE, &params, inC, 2, &outC, 1),
        HBI_OK);

    uint32_t inD[2] = {outB, outC};
    HBI_CHECK_EQ_INT(
        hbi_graph_add_node(builder, "D", HBI_KERNEL_OP_ELEMENTWISE, &params, inD, 2, &outD, 1),
        HBI_OK);

    hbi_graph *graph = NULL;
    HBI_CHECK_EQ_INT(hbi_graph_build(builder, &graph), HBI_OK);

    hbi_execution_plan *plan = NULL;
    HBI_CHECK_EQ_INT(hbi_scheduler_create_plan(sch, graph, &plan), HBI_OK);
    HBI_CHECK(plan != NULL);

    /* 4 tasks. B and C should be grouped into the same stage since they both depend on A and have
     * in_degree=0 after A. */
    HBI_CHECK_EQ_INT(plan->num_tasks, 4);
    HBI_CHECK_EQ_INT(plan->stats.total_tasks, 4);
    HBI_CHECK_EQ_INT(plan->stats.num_stages, 3); /* Stage0: A, Stage1: B+C, Stage2: D */

    HBI_CHECK_EQ_INT(plan->stages[0].num_tasks, 1);
    HBI_CHECK_EQ_INT(plan->stages[1].num_tasks, 2);
    HBI_CHECK_EQ_INT(plan->stages[2].num_tasks, 1);

    hbi_execution_plan_destroy(sch, plan);
    hbi_graph_destroy(graph);
    hbi_scheduler_destroy(sch);
}

int main(void) {
    HBI_CHECK_EQ_INT(hbi_scheduler_selftest(), HBI_OK);

    test_scheduler_creation();
    test_scheduler_plan_generation();
    test_scheduler_branching_graph();

    printf("PASS\n");
    return 0;
}
