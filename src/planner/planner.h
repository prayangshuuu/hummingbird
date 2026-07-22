/* planner.h — Memory Planner & Lifetime Analysis
 *
 * Core-public header for the `planner` module (layer 7).
 * Symbols are prefixed `hbi_` (internal, no stability guarantee).
 * See docs/architecture/11-memory-planner.md.
 */
#ifndef HB_PLANNER_H
#define HB_PLANNER_H

#include "common/common.h"
#include "graph/graph.h"
#include "memory/memory.h"
#include "tensor/tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hbi_memory_planner hbi_memory_planner;
typedef struct hbi_memory_plan hbi_memory_plan;

/* ── Data Structures ───────────────────────────────────────────────────────── */

/* Represents the lifetime of a specific graph value.
 * Lifetimes are defined by the topological execution index. */
typedef struct hbi_live_range {
    uint32_t value_id;  /* Graph value this range represents */
    uint32_t first_use; /* Topological index where it is created/first read */
    uint32_t last_use;  /* Topological index where it is last read */
    size_t size_bytes;  /* Size required by the tensor */
    size_t alignment;   /* Alignment required */
} hbi_live_range;

/* Represents a specific memory slice inside a shared buffer pool. */
typedef struct hbi_allocation {
    uint32_t value_id; /* The value being allocated */
    uint32_t pool_id;  /* The ID of the backing buffer pool */
    size_t offset;     /* Offset within the pool */
    size_t size_bytes; /* Size of the allocation */
} hbi_allocation;

/* Detailed statistics for peak memory usage and efficiency. */
typedef struct hbi_memory_statistics {
    size_t peak_memory_bytes;       /* Total peak memory required (all pools combined) */
    size_t persistent_memory_bytes; /* Memory required for weights/constants */
    size_t temporary_memory_bytes;  /* Memory required for intermediate tensors */
    size_t workspace_memory_bytes;  /* Memory required for kernel workspaces */
    size_t naive_memory_bytes;      /* Memory that would be required without reuse */
    uint32_t num_allocations;       /* Total number of intermediate allocations tracked */
    uint32_t num_pools;             /* Number of contiguous physical pools allocated */
} hbi_memory_statistics;

/* ── Memory Planner API ────────────────────────────────────────────────────── */

/* Create a new memory planner for a specific graph.
 * The graph must have been finalized via hbi_graph_build. */
hbi_status hbi_memory_planner_create(const hbi_graph *graph, hbi_memory_planner **out_planner);

/* Run lifetime analysis and buffer aliasing to generate an optimal memory plan.
 * This analyzes the graph's topological execution order. */
hbi_status hbi_memory_planner_plan(hbi_memory_planner *planner, hbi_memory_plan **out_plan);

/* Destroy the memory planner. */
void hbi_memory_planner_destroy(hbi_memory_planner *planner);

/* ── Memory Plan API ───────────────────────────────────────────────────────── */

/* Retrieve the number of distinct physical memory pools required by the plan.
 * Each pool is a single large contiguous allocation. */
uint32_t hbi_memory_plan_num_pools(const hbi_memory_plan *plan);

/* Get the required size in bytes for a specific pool. */
size_t hbi_memory_plan_pool_size(const hbi_memory_plan *plan, uint32_t pool_id);

/* Retrieve the allocation descriptor for a specific graph value.
 * Returns HBI_ERR_NOT_FOUND if the value is an input/constant and not managed
 * by the temporary memory planner. */
hbi_status hbi_memory_plan_get_allocation(const hbi_memory_plan *plan, uint32_t value_id,
                                          hbi_allocation *out_alloc);

/* Get the maximum workspace size required across all nodes. */
size_t hbi_memory_plan_workspace_size(const hbi_memory_plan *plan);

/* Get overall statistics for the generated plan. */
hbi_status hbi_memory_plan_get_statistics(const hbi_memory_plan *plan,
                                          hbi_memory_statistics *out_stats);

/* Destroy a finalized memory plan. */
void hbi_memory_plan_destroy(hbi_memory_plan *plan);

/* ── Module Identity ───────────────────────────────────────────────────────── */

/* Human-readable module name. */
const char *hbi_planner_name(void);

/* Compile-time self-check. */
hbi_status hbi_planner_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_PLANNER_H */
