/* graph.c — Forward-graph node types (RMSNORM/ATTENTION/MOE/MLP/RESIDUAL) and typed op-module
 * registry. */
#include "graph/graph_internal.h"

const char *hbi_graph_name(void) {
    return "graph";
}

hbi_status hbi_graph_selftest(void) {
    /* Scaffold: no invariants to check yet. */
    return HBI_OK;
}
