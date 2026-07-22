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

int main(void) {
    HBI_CHECK_EQ_INT(hbi_scheduler_selftest(), HBI_OK);

    test_scheduler_creation();
    test_scheduler_plan_generation();

    printf("PASS\n");
    return 0;
}
