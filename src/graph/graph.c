/* graph.c — Forward-graph node types and typed op-module registry.
 *
 * Implements the execution graph builder and immutable graph API.
 * Performs cycle detection, topological sorting, and basic shape inference.
 */
#include "graph/graph_internal.h"
#include "tensor/tensor.h"
#include <stdio.h>
#include <stdlib.h>

const char *hbi_graph_name(void) {
    return "graph";
}

hbi_status hbi_graph_selftest(void) {
    return HBI_OK;
}

/* ── Graph Builder ─────────────────────────────────────────────────────────── */

hbi_status hbi_graph_builder_create(hbi_graph_builder **out) {
    if (out == NULL)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "out is NULL");

    hbi_graph_builder *b = hbi_aligned_alloc(64, sizeof(hbi_graph_builder));
    if (b == NULL)
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate graph builder");
    memset(b, 0, sizeof(*b));

    /* Pre-allocate arrays to max capacity for simplicity in this phase */
    b->nodes = hbi_aligned_alloc(64, sizeof(hbi_node) * HBI_GRAPH_MAX_NODES);
    b->values = hbi_aligned_alloc(64, sizeof(hbi_value) * HBI_GRAPH_MAX_VALUES);

    if (!b->nodes || !b->values) {
        hbi_graph_builder_destroy(b);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate graph buffers");
    }

    *out = b;
    return HBI_OK;
}

void hbi_graph_builder_destroy(hbi_graph_builder *builder) {
    if (builder) {
        if (builder->nodes)
            hbi_aligned_free(builder->nodes);
        if (builder->values)
            hbi_aligned_free(builder->values);
        hbi_aligned_free(builder);
    }
}

static hbi_status add_value(hbi_graph_builder *builder, const char *name, const hbi_shape *shape,
                            hbi_dtype dtype, bool is_constant, hbi_tensor *const_tensor,
                            uint32_t producer, uint32_t *out_id) {
    if (builder->num_values >= HBI_GRAPH_MAX_VALUES) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "max values exceeded in graph");
    }
    uint32_t id = builder->num_values++;
    hbi_value *v = &builder->values[id];
    v->id = id;
    if (name) {
        snprintf(v->name, sizeof(v->name), "%s", name);
    } else {
        snprintf(v->name, sizeof(v->name), "v%u", id);
    }
    v->shape = *shape;
    v->dtype = dtype;
    v->is_constant = is_constant;
    v->const_tensor = const_tensor;
    v->producer_node_id = producer;

    *out_id = id;
    return HBI_OK;
}

hbi_status hbi_graph_add_input(hbi_graph_builder *builder, const char *name, const hbi_shape *shape,
                               hbi_dtype dtype, uint32_t *out_id) {
    if (!builder || !shape || !out_id)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL arg");
    if (!hbi_dtype_is_valid(dtype))
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "invalid dtype");

    return add_value(builder, name, shape, dtype, false, NULL, UINT32_MAX, out_id);
}

hbi_status hbi_graph_add_constant(hbi_graph_builder *builder, const char *name, hbi_tensor *tensor,
                                  uint32_t *out_id) {
    if (!builder || !tensor || !out_id)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL arg");

    return add_value(builder, name, hbi_tensor_shape(tensor), hbi_tensor_dtype(tensor), true,
                     tensor, UINT32_MAX, out_id);
}

/* Basic shape inference logic. Sets *out_shape and *out_dtype. */
static hbi_status infer_output_shape(hbi_kernel_op op, const hbi_kernel_params *params,
                                     const hbi_value **inputs, uint32_t num_inputs,
                                     hbi_shape *out_shape, hbi_dtype *out_dtype) {
    if (op == HBI_KERNEL_OP_COPY || op == HBI_KERNEL_OP_CAST) {
        if (num_inputs != 1)
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "requires 1 input");
        *out_shape = inputs[0]->shape;
        *out_dtype = (op == HBI_KERNEL_OP_CAST) ? params->u.cast_target : inputs[0]->dtype;
    } else if (op == HBI_KERNEL_OP_FILL) {
        if (num_inputs != 0)
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "requires 0 inputs");
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "fill shape cannot be inferred from inputs");
    } else if (op == HBI_KERNEL_OP_ELEMENTWISE) {
        if (num_inputs != 2)
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "requires 2 inputs");
        *out_dtype = inputs[0]->dtype; /* assume same dtype */
        return hbi_shape_broadcast(&inputs[0]->shape, &inputs[1]->shape, out_shape);
    } else if (op == HBI_KERNEL_OP_TRANSPOSE) {
        if (num_inputs != 1)
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "requires 1 input");
        *out_shape = inputs[0]->shape;
        uint32_t a = params->u.transpose.axis_a;
        uint32_t b = params->u.transpose.axis_b;
        if (a >= out_shape->rank || b >= out_shape->rank) {
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "transpose axis out of bounds");
        }
        int64_t tmp = out_shape->dims[a];
        out_shape->dims[a] = out_shape->dims[b];
        out_shape->dims[b] = tmp;
        *out_dtype = inputs[0]->dtype;
    } else if (op == HBI_KERNEL_OP_MATMUL) {
        if (num_inputs != 2)
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "requires 2 inputs");
        const hbi_shape *sA = &inputs[0]->shape;
        const hbi_shape *sB = &inputs[1]->shape;
        if (sA->rank != 2 || sB->rank != 2)
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "matmul requires 2D");
        if (sA->dims[1] != sB->dims[0])
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "matmul inner dim mismatch");
        out_shape->rank = 2;
        out_shape->dims[0] = sA->dims[0];
        out_shape->dims[1] = sB->dims[1];
        *out_dtype = inputs[0]->dtype;
    } else {
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "shape inference for op %d unsupported", op);
    }
    return HBI_OK;
}

hbi_status hbi_graph_add_node(hbi_graph_builder *builder, const char *name, hbi_kernel_op op,
                              const hbi_kernel_params *params, const uint32_t *inputs,
                              uint32_t num_inputs, uint32_t *outputs, uint32_t num_outputs) {
    if (!builder || !outputs)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL arg");
    if (num_inputs > 0 && !inputs)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL inputs");
    if (num_inputs > HBI_KERNEL_MAX_INPUTS || num_outputs > HBI_KERNEL_MAX_OUTPUTS) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "too many inputs/outputs");
    }
    if (builder->num_nodes >= HBI_GRAPH_MAX_NODES) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "max nodes exceeded");
    }

    uint32_t node_id = builder->num_nodes++;
    hbi_node *n = &builder->nodes[node_id];
    n->id = node_id;
    if (name) {
        snprintf(n->name, sizeof(n->name), "%s", name);
    } else {
        snprintf(n->name, sizeof(n->name), "n%u", node_id);
    }
    n->op = op;
    if (params) {
        n->params = *params;
    } else {
        memset(&n->params, 0, sizeof(n->params));
    }

    n->num_inputs = num_inputs;
    const hbi_value *val_ptrs[HBI_KERNEL_MAX_INPUTS];
    for (uint32_t i = 0; i < num_inputs; ++i) {
        if (inputs[i] >= builder->num_values) {
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "invalid input value ID");
        }
        n->inputs[i] = inputs[i];
        val_ptrs[i] = &builder->values[inputs[i]];
    }

    n->num_outputs = num_outputs;
    for (uint32_t i = 0; i < num_outputs; ++i) {
        hbi_shape out_shape = {0};
        hbi_dtype out_dtype = 0;

        hbi_status st =
            infer_output_shape(op, &n->params, val_ptrs, num_inputs, &out_shape, &out_dtype);
        if (st != HBI_OK) {
            return HBI_ERR_SETF(st, 0, "shape inference failed for node %s", n->name);
        }

        uint32_t val_id = 0;
        char vname[HBI_GRAPH_NAME_MAX];
        snprintf(vname, sizeof(vname), "%.40s_out%u", n->name, i);
        st = add_value(builder, vname, &out_shape, out_dtype, false, NULL, node_id, &val_id);
        if (st != HBI_OK)
            return st;

        n->outputs[i] = val_id;
        outputs[i] = val_id;
    }

    return HBI_OK;
}

/* Recursive DFS for cycle detection and topological sort. */
static hbi_status topo_visit(uint32_t node_id, const hbi_graph_builder *builder, uint8_t *visited,
                             uint32_t *order, uint32_t *order_idx) {
    if (visited[node_id] == 1) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "cycle detected in graph");
    }
    if (visited[node_id] == 2) {
        return HBI_OK; /* already fully processed */
    }

    visited[node_id] = 1; /* visiting */

    const hbi_node *n = &builder->nodes[node_id];
    for (uint32_t i = 0; i < n->num_inputs; ++i) {
        uint32_t val_id = n->inputs[i];
        const hbi_value *v = &builder->values[val_id];
        if (v->producer_node_id != UINT32_MAX) {
            hbi_status st = topo_visit(v->producer_node_id, builder, visited, order, order_idx);
            if (st != HBI_OK)
                return st;
        }
    }

    visited[node_id] = 2; /* visited */
    order[(*order_idx)++] = node_id;
    return HBI_OK;
}

hbi_status hbi_graph_build(hbi_graph_builder *builder, hbi_graph **out_graph) {
    if (!builder || !out_graph)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL arg");

    uint8_t *visited = hbi_aligned_alloc(64, builder->num_nodes);
    uint32_t *order = hbi_aligned_alloc(64, builder->num_nodes * sizeof(uint32_t));
    if (!visited || !order) {
        if (visited)
            hbi_aligned_free(visited);
        if (order)
            hbi_aligned_free(order);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate topo sort buffers");
    }
    memset(visited, 0, builder->num_nodes);
    uint32_t order_idx = 0;

    hbi_status st = HBI_OK;
    for (uint32_t i = 0; i < builder->num_nodes; ++i) {
        if (visited[i] == 0) {
            st = topo_visit(i, builder, visited, order, &order_idx);
            if (st != HBI_OK)
                break;
        }
    }
    hbi_aligned_free(visited);

    if (st != HBI_OK) {
        hbi_aligned_free(order);
        return st;
    }

    hbi_graph *g = hbi_aligned_alloc(64, sizeof(hbi_graph));
    if (!g) {
        hbi_aligned_free(order);
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate graph");
    }

    g->nodes = builder->nodes;
    g->num_nodes = builder->num_nodes;
    g->values = builder->values;
    g->num_values = builder->num_values;
    g->execution_order = order;
    g->num_execution_order = builder->num_nodes;

    *out_graph = g;

    /* Transfer ownership, prevent double free */
    builder->nodes = NULL;
    builder->values = NULL;
    hbi_graph_builder_destroy(builder);

    return HBI_OK;
}

/* ── Immutable Graph API ───────────────────────────────────────────────────── */

void hbi_graph_destroy(hbi_graph *graph) {
    if (graph) {
        if (graph->nodes)
            hbi_aligned_free(graph->nodes);
        if (graph->values)
            hbi_aligned_free(graph->values);
        if (graph->execution_order)
            hbi_aligned_free(graph->execution_order);
        hbi_aligned_free(graph);
    }
}

uint32_t hbi_graph_num_values(const hbi_graph *graph) {
    return graph ? graph->num_values : 0;
}

uint32_t hbi_graph_num_nodes(const hbi_graph *graph) {
    return graph ? graph->num_nodes : 0;
}

const hbi_value *hbi_graph_value_at(const hbi_graph *graph, uint32_t id) {
    if (!graph || id >= graph->num_values)
        return NULL;
    return &graph->values[id];
}

const hbi_node *hbi_graph_node_at(const hbi_graph *graph, uint32_t id) {
    if (!graph || id >= graph->num_nodes)
        return NULL;
    return &graph->nodes[id];
}

const uint32_t *hbi_graph_execution_order(const hbi_graph *graph) {
    return graph ? graph->execution_order : NULL;
}
