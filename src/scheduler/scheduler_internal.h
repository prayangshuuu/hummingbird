/* scheduler_internal.h — Internal state for Scheduler (RFC-007) */
#ifndef HB_SCHEDULER_INTERNAL_H
#define HB_SCHEDULER_INTERNAL_H

#include "scheduler.h"

struct hbi_scheduler {
    hbi_allocator *allocator;
};

/* Helper for building the plan */
typedef struct hbi_plan_builder {
    hbi_scheduler *scheduler;
    const hbi_graph *graph;
    hbi_execution_plan *plan;

    /* Temp storage during topological sort/cycle detection */
    uint8_t *visited; /* 0: unvisited, 1: visiting, 2: visited */
    uint32_t *sorted_ids;
    uint32_t sorted_count;
} hbi_plan_builder;

#endif /* HB_SCHEDULER_INTERNAL_H */
