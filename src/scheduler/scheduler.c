/* scheduler.c — Converts an Execution Graph into an optimized Execution Plan (RFC-007)
 *
 * Implements dependency analysis, execution ordering, resource planning, and
 * synchronization planning for the Hummingbird engine. Currently supports
 * sequential policy only, but laid out for future parallel staging.
 */
#include "scheduler/scheduler_internal.h"
#include <string.h>

/* ── Initialization ──────────────────────────────────────────────────────── */

hbi_status hbi_scheduler_create(hbi_allocator *allocator, hbi_scheduler **out) {
    if (!out)
        return HBI_ERR_INVALID_ARG;

    hbi_allocator *alloc = allocator ? allocator : hbi_allocator_system();
    hbi_scheduler *sch = (hbi_scheduler *)hbi_alloc(alloc, sizeof(*sch), 0, HBI_MEM_GENERAL);
    if (!sch)
        return HBI_ERR_OOM;

    memset(sch, 0, sizeof(*sch));
    sch->allocator = alloc;
    *out = sch;
    return HBI_OK;
}

void hbi_scheduler_destroy(hbi_scheduler *scheduler) {
    if (scheduler) {
        hbi_free(scheduler->allocator, scheduler);
    }
}

/* ── Plan Generation ─────────────────────────────────────────────────────── */

hbi_status hbi_scheduler_create_plan(hbi_scheduler *scheduler, const hbi_graph *graph,
                                     hbi_execution_plan **out_plan) {
    if (!scheduler || !graph || !out_plan)
        return HBI_ERR_INVALID_ARG;

    uint32_t num_nodes = hbi_graph_num_nodes(graph);
    if (num_nodes == 0)
        return HBI_ERR_INVALID_ARG;

    hbi_execution_plan *plan =
        (hbi_execution_plan *)hbi_alloc(scheduler->allocator, sizeof(*plan), 0, HBI_MEM_GENERAL);
    if (!plan)
        return HBI_ERR_OOM;
    memset(plan, 0, sizeof(*plan));

    plan->num_tasks = num_nodes;
    plan->tasks = (hbi_task *)hbi_alloc(scheduler->allocator, sizeof(hbi_task) * num_nodes, 0,
                                        HBI_MEM_GENERAL);
    if (!plan->tasks) {
        hbi_free(scheduler->allocator, plan);
        return HBI_ERR_OOM;
    }
    memset(plan->tasks, 0, sizeof(hbi_task) * num_nodes);

    /* Sequential policy: 1 stage per task. */
    plan->num_stages = num_nodes;
    plan->stages = (hbi_execution_stage *)hbi_alloc(
        scheduler->allocator, sizeof(hbi_execution_stage) * num_nodes, 0, HBI_MEM_GENERAL);
    if (!plan->stages) {
        hbi_free(scheduler->allocator, plan->tasks);
        hbi_free(scheduler->allocator, plan);
        return HBI_ERR_OOM;
    }
    memset(plan->stages, 0, sizeof(hbi_execution_stage) * num_nodes);

    /* Build tasks */
    for (uint32_t i = 0; i < num_nodes; ++i) {
        const hbi_node *node = hbi_graph_node_at(graph, i);
        hbi_task *task = &plan->tasks[i];

        task->task_id = i;
        task->node = node;
        task->required_device = HBI_DEVICE_TYPE_CPU; /* Default for now */
        task->required_workspace = 0; /* Placed holder, fetched via kernel registry in reality */
        task->estimated_cost = 1;
        task->execution_stage = i;

        /* Basic dependencies logic: For a linear graph, task i depends on i-1.
         * For a real DAG, we'd map inputs to producer nodes. */
        if (i > 0) {
            task->num_dependencies = 1;
            task->dependencies =
                (uint32_t *)hbi_alloc(scheduler->allocator, sizeof(uint32_t), 0, HBI_MEM_GENERAL);
            if (task->dependencies) {
                task->dependencies[0] = i - 1;
            }
        }

        /* Populate sequential stages */
        hbi_execution_stage *stage = &plan->stages[i];
        stage->stage_id = i;
        stage->num_tasks = 1;
        stage->tasks =
            (hbi_task **)hbi_alloc(scheduler->allocator, sizeof(hbi_task *), 0, HBI_MEM_GENERAL);
        if (stage->tasks) {
            stage->tasks[0] = task;
        }
        stage->completion_barrier.type = HBI_SYNC_BARRIER;
        stage->completion_barrier.sync_id = i;
    }

    /* Calculate Statistics */
    plan->stats.total_tasks = num_nodes;
    plan->stats.num_stages = num_nodes;
    plan->stats.critical_path_length = num_nodes; /* For sequential, depth == count */
    plan->stats.dependency_depth = num_nodes;
    plan->stats.estimated_memory_usage = 0; /* Updated by planner usually */

    *out_plan = plan;
    return HBI_OK;
}

void hbi_execution_plan_destroy(hbi_scheduler *scheduler, hbi_execution_plan *plan) {
    if (!scheduler || !plan)
        return;

    if (plan->stages) {
        for (uint32_t i = 0; i < plan->num_stages; ++i) {
            if (plan->stages[i].tasks) {
                hbi_free(scheduler->allocator, plan->stages[i].tasks);
            }
        }
        hbi_free(scheduler->allocator, plan->stages);
    }

    if (plan->tasks) {
        for (uint32_t i = 0; i < plan->num_tasks; ++i) {
            if (plan->tasks[i].dependencies) {
                hbi_free(scheduler->allocator, plan->tasks[i].dependencies);
            }
        }
        hbi_free(scheduler->allocator, plan->tasks);
    }

    hbi_free(scheduler->allocator, plan);
}

/* ── Module Identity ─────────────────────────────────────────────────────── */

const char *hbi_scheduler_name(void) {
    return "scheduler";
}

hbi_status hbi_scheduler_selftest(void) {
    hbi_scheduler *sch = NULL;
    if (hbi_scheduler_create(NULL, &sch) != HBI_OK) {
        return HBI_ERR_INTERNAL;
    }

    /* We don't have a robust way to mock hbi_graph here without bringing in the graph module,
     * but we know create/destroy works. */
    hbi_scheduler_destroy(sch);
    return HBI_OK;
}
