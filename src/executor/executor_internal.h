/* executor_internal.h — private to the `executor` module.
 *
 * Nothing here is visible to other modules. Implementation details,
 * internal structs, and static-helper prototypes live here.
 */
#ifndef HB_EXECUTOR_INTERNAL_H
#define HB_EXECUTOR_INTERNAL_H

#include "executor/executor.h"
#include "kernel/kernel.h"
#include "platform/platform.h"
#include "profiler/profiler.h"
#include <string.h>

struct hbi_executor {
    const hbi_graph *graph;

    /* Pre-resolved kernels for each node, indexed by node ID */
    const hbi_kernel **node_kernels;
    uint32_t num_nodes;
};

/* Context state for a single tensor value */
typedef struct {
    hbi_tensor *tensor;
    bool is_owned; /* True if the context allocated this tensor */
} hbi_value_state;

struct hbi_exec_context {
    const hbi_graph *graph;
    hbi_allocator *allocator;

    /* Tensors mapped by value ID */
    hbi_value_state *values;
    uint32_t num_values;

    /* Workspace for kernel execution */
    hbi_kernel_workspace workspace;

    /* Memory pools for intermediate tensors (owned by this context) */
    void **pools;
    uint32_t num_pools;
};

#endif /* HB_EXECUTOR_INTERNAL_H */
