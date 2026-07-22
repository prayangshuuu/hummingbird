/* bench_scheduler.c — Measures planning time, execution plan generation, dependency analysis */
#include "graph/graph.h"
#include "scheduler/scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Cross-platform microsecond timer (rough approximation) */
static double get_time_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

int main(void) {
    printf("--- Scheduler Benchmark (RFC-007) ---\n");

    hbi_scheduler *sch = NULL;
    if (hbi_scheduler_create(NULL, &sch) != HBI_OK) {
        printf("Failed to create scheduler.\n");
        return 1;
    }

    /* 1. Generate a large mock transformer-like graph with 10,000 nodes */
    hbi_graph_builder *builder = NULL;
    hbi_graph_builder_create(&builder);

    hbi_shape shape = {2, {1024, 1024}};
    uint32_t current_val;
    hbi_graph_add_input(builder, "input_tensor", &shape, HBI_DTYPE_FP32, &current_val);

    const uint32_t NUM_LAYERS = 1000;

    double build_start = get_time_ms();

    hbi_kernel_params params = {0};
    params.u.elementwise = HBI_ELEMENTWISE_ADD;

    for (uint32_t i = 0; i < NUM_LAYERS; ++i) {
        uint32_t out1, out2;
        /* Simulated Attention Block */
        uint32_t inputs1[2] = {current_val, current_val};
        hbi_graph_add_node(builder, "matmul_qkv", HBI_KERNEL_OP_MATMUL, &params, inputs1, 2, &out1,
                           1);

        uint32_t inputs2[2] = {out1, current_val};
        hbi_graph_add_node(builder, "add_residual", HBI_KERNEL_OP_ELEMENTWISE, &params, inputs2, 2,
                           &out2, 1);

        /* Simulated MLP Block */
        uint32_t out3, out4;
        uint32_t inputs3[2] = {out2, out2};
        hbi_graph_add_node(builder, "matmul_mlp", HBI_KERNEL_OP_MATMUL, &params, inputs3, 2, &out3,
                           1);

        uint32_t inputs4[2] = {out3, out2};
        hbi_graph_add_node(builder, "add_residual", HBI_KERNEL_OP_ELEMENTWISE, &params, inputs4, 2,
                           &out4, 1);

        current_val = out4;
    }

    hbi_graph *graph = NULL;
    hbi_graph_build(builder, &graph);

    double build_end = get_time_ms();

    printf("Graph nodes: %u\n", hbi_graph_num_nodes(graph));
    printf("Graph generation time: %.3f ms\n", build_end - build_start);

    /* 2. Run the scheduler */
    hbi_execution_plan *plan = NULL;

    double plan_start = get_time_ms();
    hbi_status status = hbi_scheduler_create_plan(sch, graph, &plan);
    double plan_end = get_time_ms();

    if (status != HBI_OK) {
        printf("Failed to generate plan!\n");
        return 1;
    }

    printf("Execution Plan generated in: %.3f ms\n", plan_end - plan_start);
    printf("Total tasks: %u\n", plan->stats.total_tasks);
    printf("Execution Stages (parallel limits): %u\n", plan->stats.num_stages);
    printf("Critical Path Depth: %u\n", plan->stats.critical_path_length);

    hbi_execution_plan_destroy(sch, plan);
    hbi_graph_destroy(graph);
    hbi_scheduler_destroy(sch);

    return 0;
}
