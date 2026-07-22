/* graph.h — Forward-graph node types and typed op-module registry.
 *
 * Core-public header for the `graph` module (layer 6). Other modules include this;
 * external embedders use <hummingbird/hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture/10-execution-graph.md.
 *
 * This module defines the declarative computation graph. It owns the structures
 * to build, validate, and represent a directed acyclic graph (DAG) of tensor
 * operations. It contains NO execution logic (that is `executor`, layer 7).
 */
#ifndef HB_GRAPH_H
#define HB_GRAPH_H

#include "common/common.h"
#include "kernel/kernel.h"
#include "tensor/tensor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum length of a symbolic value/node name, including NUL. */
#define HBI_GRAPH_NAME_MAX 64u

/* ── Graph Structures ──────────────────────────────────────────────────────── */

/* Opaque forward declarations. */
typedef struct hbi_graph hbi_graph;
typedef struct hbi_graph_builder hbi_graph_builder;

/* A symbolic value in the graph (representing a tensor).
 * It acts as an edge between nodes. Input and constant values have no producer. */
typedef struct hbi_value {
    uint32_t id;                   /* Unique index in the graph's value table */
    char name[HBI_GRAPH_NAME_MAX]; /* Human-readable diagnostic name */
    hbi_shape shape;               /* Inferred or specified shape */
    hbi_dtype dtype;               /* Data type */
    bool is_constant;              /* True if backed by a static constant tensor */
    hbi_tensor *const_tensor;      /* Borrowed pointer to constant data if is_constant */
    uint32_t producer_node_id;     /* Node ID that produces this value; UINT32_MAX if input/const */
} hbi_value;

/* A computation node in the graph. */
typedef struct hbi_node {
    uint32_t id;                   /* Unique index in the graph's node table */
    char name[HBI_GRAPH_NAME_MAX]; /* Human-readable diagnostic name */
    hbi_kernel_op op;              /* Operation taxonomy (e.g., MATMUL) */
    hbi_kernel_params params;      /* Operation-specific parameters */

    /* Input edges (references to values) */
    uint32_t inputs[HBI_KERNEL_MAX_INPUTS];
    uint32_t num_inputs;

    /* Output edges (references to values produced by this node) */
    uint32_t outputs[HBI_KERNEL_MAX_OUTPUTS];
    uint32_t num_outputs;
} hbi_node;

/* ── Graph Builder API ─────────────────────────────────────────────────────── */

/* Create a new empty graph builder. Fails HBI_ERR_OOM if allocation fails. */
hbi_status hbi_graph_builder_create(hbi_graph_builder **out);

/* Destroy a graph builder and all its un-finalized state. NULL-safe. */
void hbi_graph_builder_destroy(hbi_graph_builder *builder);

/* Add a graph-level input. An input is a placeholder for a tensor provided at
 * runtime. Writes the assigned value ID to *out_id. */
hbi_status hbi_graph_add_input(hbi_graph_builder *builder, const char *name, const hbi_shape *shape,
                               hbi_dtype dtype, uint32_t *out_id);

/* Add a constant value to the graph. The tensor buffer is BORROWED and must
 * outlive the finalized graph. */
hbi_status hbi_graph_add_constant(hbi_graph_builder *builder, const char *name, hbi_tensor *tensor,
                                  uint32_t *out_id);

/* Add a computation node.
 * Validation occurs here: arity, shape inference, dtype compatibility, etc.
 * The outputs array receives the newly created value IDs. */
hbi_status hbi_graph_add_node(hbi_graph_builder *builder, const char *name, hbi_kernel_op op,
                              const hbi_kernel_params *params, const uint32_t *inputs,
                              uint32_t num_inputs, uint32_t *outputs, uint32_t num_outputs);

/* Finalize the graph. Performs cycle detection and topological sorting.
 * Transfers ownership of the internal state to a new immutable hbi_graph.
 * The builder is destroyed on success (and *builder becomes invalid).
 * On failure, the builder is kept intact and the caller must destroy it. */
hbi_status hbi_graph_build(hbi_graph_builder *builder, hbi_graph **out_graph);

/* ── Immutable Graph API ───────────────────────────────────────────────────── */

/* Destroy a finalized graph. NULL-safe. */
void hbi_graph_destroy(hbi_graph *graph);

/* Introspection accessors. */
uint32_t hbi_graph_num_values(const hbi_graph *graph);
uint32_t hbi_graph_num_nodes(const hbi_graph *graph);
const hbi_value *hbi_graph_value_at(const hbi_graph *graph, uint32_t id);
const hbi_node *hbi_graph_node_at(const hbi_graph *graph, uint32_t id);

/* Get the topologically sorted node execution order.
 * Returns an array of node IDs of length `hbi_graph_num_nodes`. */
const uint32_t *hbi_graph_execution_order(const hbi_graph *graph);

/* ── Module Identity ───────────────────────────────────────────────────────── */

/* Human-readable module name. Never NULL. */
const char *hbi_graph_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_graph_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_GRAPH_H */
