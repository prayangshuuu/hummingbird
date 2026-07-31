/* pipeline.c — Build the computation graph and compile the execution plan
 * (RFC-019).
 *
 * Implements hbi_runtime_pipeline_build().
 *
 * The pipeline performs three real steps:
 *
 *   1. Ask the adapter to populate a graph builder with the model's forward
 *      computation graph (adapter->build_graph).
 *   2. Finalize the graph (hbi_graph_build).
 *   3. Create a scheduler and compile the graph into an execution plan
 *      (hbi_scheduler_create_plan).
 *   4. Create a backend manager so the executor can obtain contexts.
 *
 * The planner (hbi_memory_planner_*) is a separate sub-system that could be
 * integrated here in a future milestone to perform buffer aliasing.  For now
 * the scheduler already provides resource requirements that the executor
 * respects.
 *
 * Extension point — Streaming Engine (RFC-020):
 *   After step 2, a future streaming-aware pass could annotate graph nodes
 *   with prefetch hints.  hbi_runtime_pipeline_build() is the right location
 *   for that injection.
 *
 * Extension point — CUDA / Metal:
 *   Step 4 creates a backend_manager that forwards to whichever backend is
 *   currently registered.  When a CUDA backend is registered at init time,
 *   hbi_backend_manager_get_context() will automatically obtain a CUDA context.
 */
#include "runtime/pipeline.h"
#include "planner/planner.h"
#include "runtime/session.h"
#include "stream/stream.h"
#include <stdatomic.h>

hbi_status hbi_runtime_pipeline_build(hbi_runtime_session *s) {
    if (!s) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "pipeline_build: NULL session");
    }
    if (!s->adapter) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "pipeline_build: session has no adapter");
    }

    hbi_status st;

    /* ── Step 1: build the graph ────────────────────────────────────────── */

    hbi_graph_builder *builder = NULL;
    st = hbi_graph_builder_create(&builder);
    if (st != HBI_OK)
        return st;

    st = s->adapter->build_graph(s->adapter, builder, &s->descriptor);
    if (st != HBI_OK) {
        hbi_graph_builder_destroy(builder);
        return st;
    }

    /* hbi_graph_build() destroys the builder on success; on failure the
     * builder is kept intact and we destroy it ourselves. */
    hbi_graph *graph = NULL;
    st = hbi_graph_build(builder, &graph);
    if (st != HBI_OK) {
        hbi_graph_builder_destroy(builder);
        return st;
    }
    s->graph = graph;

    /* ── Step 2: compile the execution plan ─────────────────────────────── */

    hbi_scheduler *sched = NULL;
    st = hbi_scheduler_create(s->allocator, &sched);
    if (st != HBI_OK) {
        hbi_graph_destroy(s->graph);
        s->graph = NULL;
        return st;
    }
    s->scheduler = sched;

    hbi_execution_plan *plan = NULL;
    st = hbi_scheduler_create_plan(sched, graph, &plan);
    if (st != HBI_OK) {
        /* scheduler destroy also frees the plan if partially built */
        hbi_scheduler_destroy(s->scheduler);
        s->scheduler = NULL;
        hbi_graph_destroy(s->graph);
        s->graph = NULL;
        return st;
    }
    s->plan = plan;

    hbi_memory_planner *planner = NULL;
    st = hbi_memory_planner_create(s->graph, &planner);
    if (st != HBI_OK) {
        hbi_execution_plan_destroy(s->scheduler, s->plan);
        s->plan = NULL;
        hbi_scheduler_destroy(s->scheduler);
        s->scheduler = NULL;
        hbi_graph_destroy(s->graph);
        s->graph = NULL;
        return st;
    }
    s->planner = planner;

    hbi_memory_plan *plan_memory = NULL;
    st = hbi_memory_planner_plan(s->planner, &plan_memory);
    if (st != HBI_OK) {
        hbi_memory_planner_destroy(s->planner);
        s->planner = NULL;
        hbi_execution_plan_destroy(s->scheduler, s->plan);
        s->plan = NULL;
        hbi_scheduler_destroy(s->scheduler);
        s->scheduler = NULL;
        hbi_graph_destroy(s->graph);
        s->graph = NULL;
        return st;
    }
    s->plan_memory = plan_memory;

    /* ── Extension Point: Streaming Engine (RFC-020) ────────────────────── */
    /* Loop over the execution plan and prefetch tensors that will be used soon. */
    if (s->stream_engine) {
        for (uint32_t i = 0; i < s->plan->num_stages; ++i) {
            /* Check for cancellation before scheduling prefetches */
            if (atomic_load_explicit(&s->cancel_flag, memory_order_acquire)) {
                break;
            }

            hbi_execution_stage *stage = &s->plan->stages[i];
            for (uint32_t j = 0; j < stage->num_tasks; ++j) {
                hbi_task *task = stage->tasks[j];
                const hbi_node *node = task->node;
                for (uint32_t k = 0; k < node->num_inputs; ++k) {
                    uint32_t value_id = node->inputs[k];
                    hbi_stream_block *block = hbi_stream_get_block(s->stream_engine, value_id);
                    if (block) {
                        hbi_stream_prefetch(s->stream_engine, block, HB_TIER_VRAM);
                    }
                }
            }
        }
    }

    /* ── Step 3: create a backend manager ───────────────────────────────── */

    hbi_backend_manager *bm = NULL;
    st = hbi_backend_manager_create(s->allocator, &bm);
    if (st != HBI_OK) {
        hbi_memory_planner_destroy(s->planner);
        s->planner = NULL;
        s->plan_memory = NULL;
        hbi_execution_plan_destroy(s->scheduler, s->plan);
        s->plan = NULL;
        hbi_scheduler_destroy(s->scheduler);
        s->scheduler = NULL;
        hbi_graph_destroy(s->graph);
        s->graph = NULL;
        return st;
    }
    s->backend_mgr = bm;

    return HBI_OK;
}
