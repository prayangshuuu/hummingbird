/* graph_internal.h — private to the `graph` module.
 *
 * Nothing here is visible to other modules. Implementation details,
 * internal structs, and static-helper prototypes live here as the module grows.
 */
#ifndef HB_GRAPH_INTERNAL_H
#define HB_GRAPH_INTERNAL_H

#include "common/common.h"
#include "graph/graph.h"
#include "platform/platform.h" /* for hbi_aligned_alloc */
#include <string.h>

#define HBI_GRAPH_MAX_NODES 1024u
#define HBI_GRAPH_MAX_VALUES 4096u

struct hbi_graph {
    hbi_node *nodes;
    uint32_t num_nodes;

    hbi_value *values;
    uint32_t num_values;

    uint32_t *execution_order;
    uint32_t num_execution_order;
};

struct hbi_graph_builder {
    hbi_node *nodes;
    uint32_t num_nodes;

    hbi_value *values;
    uint32_t num_values;
};

#endif /* HB_GRAPH_INTERNAL_H */
