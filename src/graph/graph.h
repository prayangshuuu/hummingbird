/* graph.h — Forward-graph node types (RMSNORM/ATTENTION/MOE/MLP/RESIDUAL) and typed op-module
 * registry.
 *
 * Core-public header for the `graph` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_GRAPH_H
#define HB_GRAPH_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_graph_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_graph_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_GRAPH_H */
