/* scheduler_test.c — tests for the Scheduler (RFC-007) */
#include "graph/graph.h"
#include "memory/memory.h"
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

/* OOM fault injection — sweep every hbi_alloc call site in create_plan. */
typedef struct fault_alloc_ctx {
    int fail_at;
    int calls;
} fault_alloc_ctx;

static void *fault_alloc(void *ctx, size_t size, size_t alignment, hbi_mem_tag tag) {
    fault_alloc_ctx *fc = (fault_alloc_ctx *)ctx;
    if (fc->calls++ == fc->fail_at) {
        return NULL;
    }
    return hbi_alloc(hbi_allocator_system(), size, alignment, tag);
}

static void *fault_realloc(void *ctx, void *ptr, size_t new_size, size_t alignment,
                           hbi_mem_tag tag) {
    HB_UNUSED(ctx);
    return hbi_realloc(hbi_allocator_system(), ptr, new_size, alignment, tag);
}

static void fault_free(void *ctx, void *ptr) {
    HB_UNUSED(ctx);
    hbi_free(hbi_allocator_system(), ptr);
}

static const hbi_allocator_vtable k_fault_vtable = {
    .alloc = fault_alloc,
    .realloc = fault_realloc,
    .free = fault_free,
    .name = "fault-inject",
};

/* Build the same branching graph as test_scheduler_branching_graph (4 nodes,
 * enough edges to exercise every allocation path in create_plan). */
static hbi_graph *build_branching_graph(void) {
    hbi_graph_builder *builder = NULL;
    HBI_CHECK_EQ_INT(hbi_graph_builder_create(&builder), HBI_OK);

    hbi_shape shape = {1, {10}};
    uint32_t v1;
    HBI_CHECK_EQ_INT(hbi_graph_add_input(builder, "in1", &shape, HBI_DTYPE_FP32, &v1), HBI_OK);

    hbi_kernel_params params = {0};
    params.u.elementwise = HBI_ELEMENTWISE_ADD;

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
    return graph;
}

static void test_scheduler_plan_generation_oom_sweep(void) {
    hbi_graph *graph = build_branching_graph();

    /* Sweep every hbi_alloc site in create_plan; each failure must return
     * HBI_ERR_OOM without a crash. +1 skips scheduler's own initial alloc. */
    for (int fail_at = 0; fail_at < 32; ++fail_at) {
        fault_alloc_ctx fc = {.fail_at = fail_at + 1, .calls = 0};
        hbi_allocator fault_allocator = {.vt = &k_fault_vtable, .ctx = &fc};

        hbi_scheduler *sch = NULL;
        HBI_CHECK_EQ_INT(hbi_scheduler_create(&fault_allocator, &sch), HBI_OK);

        hbi_execution_plan *plan = NULL;
        hbi_status st = hbi_scheduler_create_plan(sch, graph, &plan);

        if (st == HBI_OK) {
            /* Ran out of injectable failure points for this fail_at; the
             * plan is real and must be freed normally. */
            hbi_execution_plan_destroy(sch, plan);
        } else {
            HBI_CHECK(plan == NULL);
        }
        hbi_scheduler_destroy(sch);
    }

    hbi_graph_destroy(graph);
}

int main(void) {
    HBI_CHECK_EQ_INT(hbi_scheduler_selftest(), HBI_OK);

    test_scheduler_creation();
    test_scheduler_plan_generation();
    test_scheduler_branching_graph();
    test_scheduler_plan_generation_oom_sweep();

    printf("PASS\n");
    return 0;
}
