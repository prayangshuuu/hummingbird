/* executor.c — Walks a forward graph and dispatches each op node to its typed module.
 *
 * Implements graph execution and context management.
 */
#include "executor/executor_internal.h"
#include <stdio.h>
#include <stdlib.h>

const char *hbi_executor_name(void) {
    return "executor";
}

hbi_status hbi_executor_selftest(void) {
    return HBI_OK;
}

/* ── Execution Context ─────────────────────────────────────────────────────── */

hbi_status hbi_exec_context_create(const hbi_graph *graph, hbi_allocator *allocator,
                                   hbi_exec_context **out) {
    if (!graph || !out)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL arg");

    hbi_exec_context *ctx = hbi_aligned_alloc(64, sizeof(hbi_exec_context));
    if (!ctx)
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate context");
    memset(ctx, 0, sizeof(*ctx));

    ctx->graph = graph;
    ctx->allocator = allocator;
    uint32_t nv = hbi_graph_num_values(graph);
    ctx->num_values = nv;

    if (nv > 0) {
        ctx->values = hbi_aligned_alloc(64, sizeof(hbi_value_state) * nv);
        if (!ctx->values) {
            hbi_aligned_free(ctx);
            return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate context values");
        }
        memset(ctx->values, 0, sizeof(hbi_value_state) * nv);
    }

    hbi_status st = hbi_kernel_workspace_init(&ctx->workspace, allocator);
    if (st != HBI_OK) {
        if (ctx->values)
            hbi_aligned_free(ctx->values);
        hbi_aligned_free(ctx);
        return st;
    }

    /* Pre-bind constants */
    for (uint32_t i = 0; i < nv; ++i) {
        const hbi_value *v = hbi_graph_value_at(graph, i);
        if (v->is_constant && v->const_tensor) {
            ctx->values[i].tensor = v->const_tensor;
            ctx->values[i].is_owned = false;
        }
    }

    *out = ctx;
    return HBI_OK;
}

hbi_status hbi_exec_context_bind(hbi_exec_context *ctx, uint32_t value_id, hbi_tensor *tensor) {
    if (!ctx || !tensor)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL arg");
    if (value_id >= ctx->num_values)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "invalid value ID");

    if (ctx->values[value_id].is_owned && ctx->values[value_id].tensor) {
        hbi_tensor_destroy(ctx->values[value_id].tensor);
    }

    ctx->values[value_id].tensor = tensor;
    ctx->values[value_id].is_owned = false;
    return HBI_OK;
}

hbi_status hbi_exec_context_allocate_internals(hbi_exec_context *ctx, const hbi_memory_plan *plan) {
    if (!ctx || !plan)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL arg");

    /* Allocate the physical pools from the planner */
    uint32_t num_pools = hbi_memory_plan_num_pools(plan);
    if (num_pools > 0) {
        ctx->pools = hbi_aligned_alloc(64, sizeof(void *) * num_pools);
        if (!ctx->pools)
            return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate pool array");
        ctx->num_pools = num_pools;

        for (uint32_t i = 0; i < num_pools; ++i) {
            size_t size = hbi_memory_plan_pool_size(plan, i);
            ctx->pools[i] = hbi_aligned_alloc(HBI_TENSOR_DEFAULT_ALIGN, size);
            if (!ctx->pools[i]) {
                return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate physical pool memory");
            }
        }
    }

    /* Bind tensors to the allocated pools according to the plan */
    for (uint32_t i = 0; i < ctx->num_values; ++i) {
        if (ctx->values[i].tensor == NULL) {
            hbi_allocation alloc;
            hbi_status st = hbi_memory_plan_get_allocation(plan, i, &alloc);

            if (st == HBI_OK) {
                const hbi_value *v = hbi_graph_value_at(ctx->graph, i);
                hbi_tensor *t = hbi_aligned_alloc(64, sizeof(hbi_tensor));
                if (!t)
                    return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to alloc tensor struct");

                void *ptr = (uint8_t *)ctx->pools[alloc.pool_id] + alloc.offset;
                st = hbi_tensor_wrap(t, v->dtype, &v->shape, ptr, alloc.size_bytes);
                if (st != HBI_OK) {
                    hbi_aligned_free(t);
                    return HBI_ERR_SETF(st, 0, "failed to wrap tensor for %s", v->name);
                }

                ctx->values[i].tensor = t;
                ctx->values[i].is_owned =
                    true; /* We own the struct, but NOT the buffer (wrap makes it BORROWED) */
            }
        }
    }
    return HBI_OK;
}

void hbi_exec_context_destroy(hbi_exec_context *ctx) {
    if (ctx) {
        hbi_kernel_workspace_destroy(&ctx->workspace);
        if (ctx->values) {
            for (uint32_t i = 0; i < ctx->num_values; ++i) {
                if (ctx->values[i].is_owned && ctx->values[i].tensor) {
                    hbi_tensor_destroy(ctx->values[i].tensor); /* Safe: only zeroes if BORROWED */
                    hbi_aligned_free(ctx->values[i].tensor);
                }
            }
            hbi_aligned_free(ctx->values);
        }
        if (ctx->pools) {
            for (uint32_t i = 0; i < ctx->num_pools; ++i) {
                if (ctx->pools[i]) {
                    hbi_aligned_free(ctx->pools[i]);
                }
            }
            hbi_aligned_free(ctx->pools);
        }
        hbi_aligned_free(ctx);
    }
}

/* ── Executor ──────────────────────────────────────────────────────────────── */

hbi_status hbi_executor_create(const hbi_graph *graph, hbi_executor **out) {
    if (!graph || !out)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL arg");

    hbi_executor *ex = hbi_aligned_alloc(64, sizeof(hbi_executor));
    if (!ex)
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate executor");
    memset(ex, 0, sizeof(*ex));

    ex->graph = graph;
    uint32_t nn = hbi_graph_num_nodes(graph);
    ex->num_nodes = nn;

    if (nn > 0) {
        ex->node_kernels = hbi_aligned_alloc(64, sizeof(const hbi_kernel *) * nn);
        if (!ex->node_kernels) {
            hbi_aligned_free(ex);
            return HBI_ERR_SET(HBI_ERR_OOM, 0, "failed to allocate node_kernels");
        }
        memset(ex->node_kernels, 0, sizeof(const hbi_kernel *) * nn);
    }

    /* Ahead-of-time dispatch resolution */
    for (uint32_t i = 0; i < nn; ++i) {
        const hbi_node *n = hbi_graph_node_at(graph, i);

        /* Find the first input's dtype and layout to use for dispatch */
        hbi_dtype d = HBI_DTYPE_FP32;
        if (n->num_inputs > 0) {
            const hbi_value *v = hbi_graph_value_at(graph, n->inputs[0]);
            d = v->dtype;
        } else if (n->num_outputs > 0) {
            const hbi_value *v = hbi_graph_value_at(graph, n->outputs[0]);
            d = v->dtype;
        }

        hbi_kernel_key key = {.op = n->op,
                              .device = HBI_TENSOR_DEVICE_CPU,
                              .dtype = d,
                              .layout_flags = HBI_KERNEL_LAYOUT_ANY};

        const hbi_kernel *k = NULL;
        hbi_status st = hbi_kernel_resolve(&key, &k);
        if (st != HBI_OK || k == NULL) {
            hbi_executor_destroy(ex);
            return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0,
                                "no kernel found for node %s (op %s, dtype %s)", n->name,
                                hbi_kernel_op_str(n->op), hbi_dtype_str(d));
        }
        ex->node_kernels[i] = k;
    }

    *out = ex;
    return HBI_OK;
}

void hbi_executor_destroy(hbi_executor *executor) {
    if (executor) {
        if (executor->node_kernels)
            hbi_aligned_free((void *)executor->node_kernels);
        hbi_aligned_free(executor);
    }
}

hbi_status hbi_executor_run(const hbi_executor *executor, hbi_exec_context *ctx) {
    if (!executor || !ctx)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "NULL arg");
    if (executor->graph != ctx->graph)
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "graph mismatch");

    HBI_PROF_SCOPE(prof_run, "executor.run");

    const uint32_t *order = hbi_graph_execution_order(executor->graph);
    uint32_t nn = executor->num_nodes;

    for (uint32_t i = 0; i < nn; ++i) {
        uint32_t node_id = order[i];
        const hbi_node *n = hbi_graph_node_at(executor->graph, node_id);
        const hbi_kernel *k = executor->node_kernels[node_id];

        hbi_kernel_args args;
        hbi_kernel_args_init(&args);
        args.params = n->params;

        args.num_inputs = n->num_inputs;
        for (uint32_t j = 0; j < n->num_inputs; ++j) {
            hbi_tensor *t = ctx->values[n->inputs[j]].tensor;
            if (!t)
                return HBI_ERR_SETF(HBI_ERR_STATE, 0, "unbound input %s for %s",
                                    hbi_graph_value_at(executor->graph, n->inputs[j])->name,
                                    n->name);
            args.inputs[j] = t;
        }

        args.num_outputs = n->num_outputs;
        for (uint32_t j = 0; j < n->num_outputs; ++j) {
            hbi_tensor *t = ctx->values[n->outputs[j]].tensor;
            if (!t)
                return HBI_ERR_SETF(HBI_ERR_STATE, 0, "unbound output %s for %s",
                                    hbi_graph_value_at(executor->graph, n->outputs[j])->name,
                                    n->name);
            args.outputs[j] = t;
        }

        if (k->workspace_size) {
            size_t ws_bytes = 0;
            hbi_status st = k->workspace_size(&args, &ws_bytes);
            if (st != HBI_OK)
                return HBI_ERR_SETF(st, 0, "workspace query failed on %s", n->name);

            if (ws_bytes > 0) {
                st = hbi_kernel_workspace_reserve(&ctx->workspace, ws_bytes,
                                                  HBI_TENSOR_DEFAULT_ALIGN);
                if (st != HBI_OK)
                    return HBI_ERR_SETF(st, 0, "workspace reserve failed on %s", n->name);
            }
        }

        HBI_PROF_SCOPE(prof_node, "executor.node");
        hbi_status st = k->run(&args, &ctx->workspace);
        if (st != HBI_OK) {
            return HBI_ERR_SETF(st, 0, "kernel execution failed on %s", n->name);
        }
    }

    return HBI_OK;
}
