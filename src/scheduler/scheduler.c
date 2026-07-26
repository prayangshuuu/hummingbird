/* scheduler.c — Converts an Execution Graph into an optimized Execution Plan (RFC-007)
 *
 * Implements dependency analysis, execution ordering, resource planning, and
 * synchronization planning for the Hummingbird engine. Currently supports
 * sequential policy only, but laid out for future parallel staging.
 */
#include "scheduler/scheduler_internal.h"
#include <stdlib.h>
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

/* ── Task Queue ──────────────────────────────────────────────────────────── */

static hbi_status task_queue_init(hbi_allocator *alloc, hbi_task_queue *q, uint32_t capacity) {
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->task_ids = (uint32_t *)hbi_alloc(alloc, sizeof(uint32_t) * capacity, 0, HBI_MEM_GENERAL);
    if (!q->task_ids)
        return HBI_ERR_OOM;
    return HBI_OK;
}

static void task_queue_push(hbi_task_queue *q, uint32_t task_id) {
    if (q->count < q->capacity) {
        q->task_ids[q->tail] = task_id;
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
    }
}

static uint32_t task_queue_pop(hbi_task_queue *q) {
    if (q->count > 0) {
        uint32_t id = q->task_ids[q->head];
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        return id;
    }
    return UINT32_MAX;
}

static void task_queue_destroy(hbi_allocator *alloc, hbi_task_queue *q) {
    if (q->task_ids) {
        hbi_free(alloc, q->task_ids);
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

    hbi_allocator *alloc = scheduler->allocator;
    hbi_status status = HBI_ERR_OOM;

    /* All pointers declared up front, zeroed, so `goto fail` is always safe
     * regardless of which allocation below it fails on. */
    hbi_execution_plan *plan = NULL;
    hbi_task_graph tg = {0};
    uint32_t *out_edge_capacity = NULL;
    hbi_task_queue q = {0};
    uint32_t *sorted_order = NULL;
    uint32_t sorted_count = 0;
    uint32_t max_depth = 0;

    plan = (hbi_execution_plan *)hbi_alloc(alloc, sizeof(*plan), 0, HBI_MEM_GENERAL);
    if (!plan)
        goto fail;
    memset(plan, 0, sizeof(*plan));

    plan->num_tasks = num_nodes;
    plan->tasks = (hbi_task *)hbi_alloc(alloc, sizeof(hbi_task) * num_nodes, 0, HBI_MEM_GENERAL);
    if (!plan->tasks)
        goto fail;
    memset(plan->tasks, 0, sizeof(hbi_task) * num_nodes);

    /* Each buffer is checked and zeroed right after its own allocation, not
     * batched, so a sibling failing later never leaves fail: walking a
     * pointer array (tg.out_edges) full of uninitialized garbage. */
    tg.num_tasks = num_nodes;
    tg.tasks = plan->tasks;
    tg.num_out_edges =
        (uint32_t *)hbi_alloc(alloc, sizeof(uint32_t) * num_nodes, 0, HBI_MEM_GENERAL);
    if (!tg.num_out_edges)
        goto fail;
    memset(tg.num_out_edges, 0, sizeof(uint32_t) * num_nodes);

    tg.out_edges =
        (uint32_t **)hbi_alloc(alloc, sizeof(uint32_t *) * num_nodes, 0, HBI_MEM_GENERAL);
    if (!tg.out_edges)
        goto fail;
    memset(tg.out_edges, 0, sizeof(uint32_t *) * num_nodes);

    tg.in_degree = (uint32_t *)hbi_alloc(alloc, sizeof(uint32_t) * num_nodes, 0, HBI_MEM_GENERAL);
    if (!tg.in_degree)
        goto fail;
    memset(tg.in_degree, 0, sizeof(uint32_t) * num_nodes);

    /* 1. Build Adjacency List (Edges) */
    for (uint32_t i = 0; i < num_nodes; ++i) {
        const hbi_node *node = hbi_graph_node_at(graph, i);
        plan->tasks[i].task_id = i;
        plan->tasks[i].node = node;
        plan->tasks[i].required_device = HBI_DEVICE_TYPE_CPU;

        /* Count in-degrees and allocate out-edges.
         * To avoid two passes over all edges for allocation, we can just allocate
         * a max possible outgoing edges array per node (num_nodes),
         * or precisely count them first. Let's precisely count. */
    }

    out_edge_capacity =
        (uint32_t *)hbi_alloc(alloc, sizeof(uint32_t) * num_nodes, 0, HBI_MEM_GENERAL);
    if (!out_edge_capacity)
        goto fail;
    memset(out_edge_capacity, 0, sizeof(uint32_t) * num_nodes);

    /* Pass 1: Count outgoing edges */
    for (uint32_t i = 0; i < num_nodes; ++i) {
        const hbi_node *node = hbi_graph_node_at(graph, i);
        for (uint32_t in_idx = 0; in_idx < node->num_inputs; ++in_idx) {
            uint32_t val_id = node->inputs[in_idx];
            const hbi_value *val = hbi_graph_value_at(graph, val_id);
            if (val && val->producer_node_id != UINT32_MAX) {
                out_edge_capacity[val->producer_node_id]++;
                tg.in_degree[i]++;
            }
        }
    }

    /* Allocate out_edges arrays and dependency arrays */
    for (uint32_t i = 0; i < num_nodes; ++i) {
        if (out_edge_capacity[i] > 0) {
            tg.out_edges[i] = (uint32_t *)hbi_alloc(alloc, sizeof(uint32_t) * out_edge_capacity[i],
                                                    0, HBI_MEM_GENERAL);
            if (!tg.out_edges[i])
                goto fail;
        }
        if (tg.in_degree[i] > 0) {
            plan->tasks[i].dependencies = (uint32_t *)hbi_alloc(
                alloc, sizeof(uint32_t) * tg.in_degree[i], 0, HBI_MEM_GENERAL);
            if (!plan->tasks[i].dependencies)
                goto fail;
        }
    }

    /* Pass 2: Populate edges */
    for (uint32_t i = 0; i < num_nodes; ++i) {
        const hbi_node *node = hbi_graph_node_at(graph, i);
        for (uint32_t in_idx = 0; in_idx < node->num_inputs; ++in_idx) {
            uint32_t val_id = node->inputs[in_idx];
            const hbi_value *val = hbi_graph_value_at(graph, val_id);
            if (val && val->producer_node_id != UINT32_MAX) {
                uint32_t prod = val->producer_node_id;
                tg.out_edges[prod][tg.num_out_edges[prod]++] = i;
                plan->tasks[i].dependencies[plan->tasks[i].num_dependencies++] = prod;
            }
        }
    }

    /* 2. Kahn's Algorithm for Topological Sort & Cycle Detection */
    if (task_queue_init(alloc, &q, num_nodes) != HBI_OK)
        goto fail;

    for (uint32_t i = 0; i < num_nodes; ++i) {
        if (tg.in_degree[i] == 0) {
            task_queue_push(&q, i);
        }
    }

    sorted_order = (uint32_t *)hbi_alloc(alloc, sizeof(uint32_t) * num_nodes, 0, HBI_MEM_GENERAL);
    if (!sorted_order)
        goto fail;

    /* To support future parallel stages, we group nodes popped at the same time into stages. */
    /* We can do a level-order traversal for strict staging. */

    plan->num_stages = 0;
    plan->stages = (hbi_execution_stage *)hbi_alloc(alloc, sizeof(hbi_execution_stage) * num_nodes,
                                                    0, HBI_MEM_GENERAL);
    if (!plan->stages)
        goto fail;
    memset(plan->stages, 0, sizeof(hbi_execution_stage) * num_nodes);

    while (q.count > 0) {
        uint32_t level_size = q.count;

        hbi_execution_stage *stage = &plan->stages[plan->num_stages];
        stage->stage_id = plan->num_stages;
        stage->num_tasks = level_size;
        stage->tasks =
            (hbi_task **)hbi_alloc(alloc, sizeof(hbi_task *) * level_size, 0, HBI_MEM_GENERAL);
        if (!stage->tasks)
            goto fail;
        stage->completion_barrier.type = HBI_SYNC_BARRIER;
        stage->completion_barrier.sync_id = plan->num_stages;

        for (uint32_t k = 0; k < level_size; ++k) {
            uint32_t u = task_queue_pop(&q);
            sorted_order[sorted_count++] = u;

            stage->tasks[k] = &plan->tasks[u];
            plan->tasks[u].execution_stage = plan->num_stages;

            for (uint32_t i = 0; i < tg.num_out_edges[u]; ++i) {
                uint32_t v = tg.out_edges[u][i];
                tg.in_degree[v]--;
                if (tg.in_degree[v] == 0) {
                    task_queue_push(&q, v);
                }
            }
        }
        plan->num_stages++;
        max_depth++;
    }

    if (sorted_count != num_nodes) {
        /* Cycle detected: not every task could be scheduled. */
        status = HBI_ERR_SETF(HBI_ERR_STATE, 0, "Cycle detected in execution graph");
        goto fail;
    }

    /* Calculate Statistics */
    plan->stats.total_tasks = num_nodes;
    plan->stats.num_stages = plan->num_stages;
    plan->stats.critical_path_length = max_depth;
    plan->stats.dependency_depth = max_depth;
    plan->stats.estimated_memory_usage = 0;

    /* Success: release the scratch structures the plan doesn't own. */
    task_queue_destroy(alloc, &q);
    hbi_free(alloc, out_edge_capacity);
    for (uint32_t i = 0; i < num_nodes; ++i) {
        if (tg.out_edges[i])
            hbi_free(alloc, tg.out_edges[i]);
    }
    hbi_free(alloc, tg.out_edges);
    hbi_free(alloc, tg.num_out_edges);
    hbi_free(alloc, tg.in_degree);
    hbi_free(alloc, sorted_order);

    *out_plan = plan;
    return HBI_OK;

fail:
    /* Everything below is either still NULL or was zeroed right after its
     * own allocation, so a plain NULL check is always safe here. */
    task_queue_destroy(alloc, &q);
    hbi_free(alloc, sorted_order);
    hbi_free(alloc, out_edge_capacity);
    if (tg.out_edges) {
        for (uint32_t i = 0; i < num_nodes; ++i) {
            if (tg.out_edges[i])
                hbi_free(alloc, tg.out_edges[i]);
        }
        hbi_free(alloc, tg.out_edges);
    }
    hbi_free(alloc, tg.num_out_edges);
    hbi_free(alloc, tg.in_degree);
    if (plan) {
        if (plan->stages) {
            for (uint32_t i = 0; i < plan->num_stages; ++i) {
                if (plan->stages[i].tasks)
                    hbi_free(alloc, plan->stages[i].tasks);
            }
            hbi_free(alloc, plan->stages);
        }
        if (plan->tasks) {
            for (uint32_t i = 0; i < num_nodes; ++i) {
                if (plan->tasks[i].dependencies)
                    hbi_free(alloc, plan->tasks[i].dependencies);
            }
            hbi_free(alloc, plan->tasks);
        }
        hbi_free(alloc, plan);
    }
    return status;
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
