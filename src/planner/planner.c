/* planner.c — Memory Planner & Lifetime Analysis */
#include "planner.h"
#include "graph/graph_internal.h" /* For accessing graph internal details if needed, though we can use public APIs */
#include "planner_internal.h"
#include "tensor/tensor.h"

#include <stdlib.h>
#include <string.h>

const char *hbi_planner_name(void) {
    return "hb_planner";
}

hbi_status hbi_planner_selftest(void) {
    return HBI_OK;
}

static size_t align_up(size_t val, size_t align) {
    if (align == 0)
        return val;
    return (val + align - 1) & ~(align - 1);
}

hbi_status hbi_memory_planner_create(const hbi_graph *graph, hbi_memory_planner **out_planner) {
    if (!graph || !out_planner)
        return HBI_ERR_INVALID_ARG;

    hbi_memory_planner *p = calloc(1, sizeof(*p));
    if (!p)
        return HBI_ERR_OOM;

    p->graph = graph;

    /* Pre-allocate arrays */
    uint32_t num_values = hbi_graph_num_values(graph);
    p->ranges.capacity = num_values;
    p->ranges.ranges = calloc(num_values, sizeof(hbi_live_range));
    if (!p->ranges.ranges) {
        free(p);
        return HBI_ERR_OOM;
    }

    *out_planner = p;
    return HBI_OK;
}

void hbi_memory_planner_destroy(hbi_memory_planner *planner) {
    if (!planner)
        return;
    free(planner->ranges.ranges);
    if (planner->plan.pools)
        free(planner->plan.pools);
    if (planner->plan.allocations)
        free(planner->plan.allocations);
    free(planner);
}

/* Compare by first_use (ascending), then size_bytes (descending) */
static int compare_live_ranges(const void *a, const void *b) {
    const hbi_live_range *ra = (const hbi_live_range *)a;
    const hbi_live_range *rb = (const hbi_live_range *)b;

    if (ra->first_use < rb->first_use)
        return -1;
    if (ra->first_use > rb->first_use)
        return 1;

    if (ra->size_bytes > rb->size_bytes)
        return -1;
    if (ra->size_bytes < rb->size_bytes)
        return 1;
    return 0;
}

hbi_status hbi_memory_planner_plan(hbi_memory_planner *planner, hbi_memory_plan **out_plan) {
    if (!planner || !out_plan)
        return HBI_ERR_INVALID_ARG;

    uint32_t num_nodes = hbi_graph_num_nodes(planner->graph);
    uint32_t num_values = hbi_graph_num_values(planner->graph);
    const uint32_t *order = hbi_graph_execution_order(planner->graph);

    if (!order)
        return HBI_ERR_STATE;

    /* Reset ranges */
    for (uint32_t i = 0; i < num_values; ++i) {
        planner->ranges.ranges[i].value_id = i;
        planner->ranges.ranges[i].first_use = UINT32_MAX;
        planner->ranges.ranges[i].last_use = 0;
    }

    /* 1. Liveness Analysis */
    size_t persistent_mem = 0;
    size_t naive_temp_mem = 0;
    size_t max_workspace = 0;

    for (uint32_t topo_idx = 0; topo_idx < num_nodes; ++topo_idx) {
        uint32_t node_id = order[topo_idx];
        const hbi_node *node = hbi_graph_node_at(planner->graph, node_id);

        /* Check kernel workspace size */
        size_t workspace_req =
            0; /* TODO: Fetch from actual kernel, but for now we assume 0 or placeholder */
        /* Currently Hummingbird kernels pre-reserve via workspace struct. We will track max. */
        if (workspace_req > max_workspace) {
            max_workspace = workspace_req;
        }

        /* Update inputs */
        for (uint32_t i = 0; i < node->num_inputs; ++i) {
            uint32_t vid = node->inputs[i];
            if (planner->ranges.ranges[vid].first_use == UINT32_MAX) {
                planner->ranges.ranges[vid].first_use = topo_idx;
            }
            if (topo_idx > planner->ranges.ranges[vid].last_use) {
                planner->ranges.ranges[vid].last_use = topo_idx;
            }
        }

        /* Update outputs */
        for (uint32_t i = 0; i < node->num_outputs; ++i) {
            uint32_t vid = node->outputs[i];
            if (planner->ranges.ranges[vid].first_use == UINT32_MAX) {
                planner->ranges.ranges[vid].first_use = topo_idx;
            }
            if (topo_idx > planner->ranges.ranges[vid].last_use) {
                planner->ranges.ranges[vid].last_use = topo_idx;
            }
        }
    }

    /* Collect ranges that need allocation */
    hbi_live_range *active_ranges = calloc(num_values, sizeof(hbi_live_range));
    uint32_t active_count = 0;

    for (uint32_t vid = 0; vid < num_values; ++vid) {
        const hbi_value *val = hbi_graph_value_at(planner->graph, vid);
        int64_t elems = 0;
        hbi_shape_elem_count(&val->shape, &elems);
        size_t bytes = 0;
        hbi_dtype_packed_nbytes(val->dtype, elems, &bytes);

        size_t alignment = hbi_dtype_align(val->dtype);
        if (alignment < HBI_TENSOR_DEFAULT_ALIGN) {
            alignment = HBI_TENSOR_DEFAULT_ALIGN; /* Force platform alignment */
        }

        if (val->is_constant || val->producer_node_id == UINT32_MAX) {
            persistent_mem += bytes;
            continue; /* Managed externally */
        }

        /* If a value is never read, its last_use is its first_use */
        if (planner->ranges.ranges[vid].first_use == UINT32_MAX) {
            continue; /* Dead code/unused output */
        }

        naive_temp_mem += bytes;

        hbi_live_range r = planner->ranges.ranges[vid];
        r.size_bytes = bytes;
        r.alignment = alignment;
        active_ranges[active_count++] = r;
    }

    qsort(active_ranges, active_count, sizeof(hbi_live_range), compare_live_ranges);

    /* 2. Greedy Buffer Reuse (1D First-Fit) */
    hbi_allocation *allocs = calloc(active_count, sizeof(hbi_allocation));
    if (!allocs && active_count > 0) {
        free(active_ranges);
        return HBI_ERR_OOM;
    }

    size_t peak_pool_size = 0;

    for (uint32_t i = 0; i < active_count; ++i) {
        const hbi_live_range *r = &active_ranges[i];

        size_t best_offset = 0;
        bool conflict = true;

        while (conflict) {
            best_offset = align_up(best_offset, r->alignment);
            conflict = false;

            for (uint32_t j = 0; j < i; ++j) {
                const hbi_live_range *prev_r = &active_ranges[j];
                const hbi_allocation *prev_a = &allocs[j];

                /* Do they overlap in time? */
                bool time_overlap =
                    (r->first_use <= prev_r->last_use) && (r->last_use >= prev_r->first_use);
                if (!time_overlap)
                    continue;

                /* Do they overlap in memory? */
                size_t prev_start = prev_a->offset;
                size_t prev_end = prev_a->offset + prev_a->size_bytes;
                size_t req_start = best_offset;
                size_t req_end = best_offset + r->size_bytes;

                if (req_start < prev_end && req_end > prev_start) {
                    /* Conflict! Push our offset past this allocation */
                    best_offset = prev_end;
                    conflict = true;
                    break;
                }
            }
        }

        allocs[i].value_id = r->value_id;
        allocs[i].pool_id = 0; /* Only 1 pool for now */
        allocs[i].offset = best_offset;
        allocs[i].size_bytes = r->size_bytes;

        size_t end_offset = best_offset + r->size_bytes;
        if (end_offset > peak_pool_size) {
            peak_pool_size = end_offset;
        }
    }

    free(active_ranges);

    /* 3. Finalize Plan */
    if (planner->plan.pools)
        free(planner->plan.pools);
    if (planner->plan.allocations)
        free(planner->plan.allocations);

    planner->plan.num_pools = 1;
    planner->plan.pools = calloc(1, sizeof(hbi_planner_pool));
    planner->plan.pools[0].size_bytes = peak_pool_size;

    planner->plan.num_allocations = active_count;
    planner->plan.allocations = allocs;
    planner->plan.workspace_size = max_workspace;

    planner->plan.stats.peak_memory_bytes = peak_pool_size + max_workspace;
    planner->plan.stats.persistent_memory_bytes = persistent_mem;
    planner->plan.stats.temporary_memory_bytes = peak_pool_size;
    planner->plan.stats.workspace_memory_bytes = max_workspace;
    planner->plan.stats.naive_memory_bytes = naive_temp_mem;
    planner->plan.stats.num_allocations = active_count;
    planner->plan.stats.num_pools = 1;

    *out_plan = &planner->plan;
    return HBI_OK;
}

uint32_t hbi_memory_plan_num_pools(const hbi_memory_plan *plan) {
    return plan ? plan->num_pools : 0;
}

size_t hbi_memory_plan_pool_size(const hbi_memory_plan *plan, uint32_t pool_id) {
    if (!plan || pool_id >= plan->num_pools)
        return 0;
    return plan->pools[pool_id].size_bytes;
}

hbi_status hbi_memory_plan_get_allocation(const hbi_memory_plan *plan, uint32_t value_id,
                                          hbi_allocation *out_alloc) {
    if (!plan || !out_alloc)
        return HBI_ERR_INVALID_ARG;

    for (uint32_t i = 0; i < plan->num_allocations; ++i) {
        if (plan->allocations[i].value_id == value_id) {
            *out_alloc = plan->allocations[i];
            return HBI_OK;
        }
    }
    return HBI_ERR_NOT_FOUND; /* Indicates not managed by planner (e.g. constant/input) */
}

size_t hbi_memory_plan_workspace_size(const hbi_memory_plan *plan) {
    return plan ? plan->workspace_size : 0;
}

hbi_status hbi_memory_plan_get_statistics(const hbi_memory_plan *plan,
                                          hbi_memory_statistics *out_stats) {
    if (!plan || !out_stats)
        return HBI_ERR_INVALID_ARG;
    *out_stats = plan->stats;
    return HBI_OK;
}

void hbi_memory_plan_destroy(hbi_memory_plan *plan) {
    /* The plan is owned by the planner; doing nothing here since planner destroys it,
     * but if we refactored to transfer ownership, we'd free here.
     * For now, we assume plan lifecycle is bound to planner lifecycle. */
    (void)plan;
}
