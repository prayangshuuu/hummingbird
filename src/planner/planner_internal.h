/* planner_internal.h — Internal structs for the memory planner. */
#ifndef HB_PLANNER_INTERNAL_H
#define HB_PLANNER_INTERNAL_H

#include "common/common.h"
#include "planner.h"

/* Simple dynamic array for live ranges. */
typedef struct hbi_live_range_array {
    hbi_live_range *ranges;
    uint32_t count;
    uint32_t capacity;
} hbi_live_range_array;

/* Represents a contiguous physical memory pool being built. */
typedef struct hbi_planner_pool {
    size_t size_bytes;
} hbi_planner_pool;

struct hbi_memory_plan {
    uint32_t num_pools;
    hbi_planner_pool *pools;

    uint32_t num_allocations;
    hbi_allocation *allocations;

    size_t workspace_size;
    hbi_memory_statistics stats;
};

struct hbi_memory_planner {
    const hbi_graph *graph;

    hbi_live_range_array ranges;
    hbi_memory_plan plan;
};

#endif /* HB_PLANNER_INTERNAL_H */
