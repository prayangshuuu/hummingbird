/* executor.c — Walk the execution plan and dispatch backend commands (RFC-019).
 *
 * Implements hbi_runtime_executor_run().
 *
 * The executor iterates over every stage in the compiled hbi_execution_plan
 * and issues an HBI_CMD_KERNEL_DISPATCH command to the selected backend for
 * each task.  This is the layer that connects the logical graph (defined by
 * the adapter) to real computation (performed by the backend).
 *
 * Current implementation:
 *   - Backend selection: uses hbi_backend_at(0), the first registered backend.
 *     This is correct for the CPU-only baseline; a future "capability
 *     negotiation" pass will select the best available backend per op.
 *   - Kernel dispatch: fills hbi_backend_command with type=KERNEL_DISPATCH and
 *     calls backend->execute().  The backend's kernel registry resolves the op.
 *   - Synchronisation: calls backend->sync() once after all stages.
 *
 * Extension point — CUDA / Metal:
 *   Replace hbi_backend_at(0) with a capability-based selection that prefers
 *   an accelerator backend when one is registered.
 *
 * Extension point — Streaming Engine (RFC-020):
 *   PREFETCH nodes injected by the pipeline's streaming pass would be handled
 *   here by issuing HBI_CMD_MEMCOPY_H2D commands before the dependent
 *   KERNEL_DISPATCH commands.
 */
#include "runtime/executor.h"
#include "executor/executor_internal.h"
#include "kernel/kernel.h"
#include <stdlib.h>

hbi_status hbi_runtime_executor_run(hbi_runtime_session *s) {
    if (!s) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "executor_run: NULL session");
    }
    if (!s->plan) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "executor_run: no execution plan");
    }
    if (!s->backend_mgr) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "executor_run: no backend manager");
    }

    /* Select backend: use first registered backend (CPU reference baseline). */
    if (hbi_backend_count() == 0) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "executor_run: no backends registered");
    }
    const hbi_backend *backend = hbi_backend_at(0);
    if (!backend) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "executor_run: hbi_backend_at(0) returned NULL");
    }

    /* Obtain a context for this backend from the manager. */
    hbi_backend_context *ctx = NULL;
    hbi_status st = hbi_backend_manager_get_context(s->backend_mgr, backend, &ctx);
    if (st != HBI_OK)
        return st;

    struct hbi_exec_context *exec_ctx = (struct hbi_exec_context *)s->exec_ctx;
    if (!exec_ctx) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "executor_run: no exec_ctx");
    }

    /* Walk stages → tasks in topological order. */
    const hbi_execution_plan *plan = s->plan;
    for (uint32_t stage_i = 0; stage_i < plan->num_stages; ++stage_i) {
        const hbi_execution_stage *stage = &plan->stages[stage_i];

        for (uint32_t task_i = 0; task_i < stage->num_tasks; ++task_i) {
            const hbi_task *task = stage->tasks[task_i];
            if (!task || !task->node)
                continue;

            /* Resolve input and output tensors from the context. */
            const void *inputs[HBI_KERNEL_MAX_INPUTS] = {NULL};
            void *outputs[HBI_KERNEL_MAX_OUTPUTS] = {NULL};

            for (uint32_t j = 0; j < task->node->num_inputs && j < HBI_KERNEL_MAX_INPUTS; ++j) {
                uint32_t val_id = task->node->inputs[j];
                if (val_id < exec_ctx->num_values) {
                    inputs[j] = exec_ctx->values[val_id].tensor;
                }
            }

            for (uint32_t j = 0; j < task->node->num_outputs && j < HBI_KERNEL_MAX_OUTPUTS; ++j) {
                uint32_t val_id = task->node->outputs[j];
                if (val_id < exec_ctx->num_values) {
                    outputs[j] = exec_ctx->values[val_id].tensor;
                }
            }

            /* Resolve the kernel. We use the first output's dtype, or FP32. */
            hbi_dtype k_dtype = HBI_DTYPE_FP32;
            if (task->node->num_outputs > 0 && outputs[0]) {
                k_dtype = hbi_tensor_dtype((const hbi_tensor *)outputs[0]);
            }
            hbi_kernel_key key = {.op = task->node->op,
                                  .device = HBI_TENSOR_DEVICE_CPU,
                                  .dtype = k_dtype,
                                  .layout_flags = HBI_KERNEL_LAYOUT_ANY};
            const hbi_kernel *kernel = NULL;
            hbi_status res_st = hbi_kernel_resolve(&key, &kernel);
            if (res_st != HBI_OK) {
                return HBI_ERR_SETF(res_st, 0, "executor_run: failed to resolve kernel for op %d",
                                    task->node->op);
            }

            /* Build the dispatch command for this graph node. */
            hbi_backend_command cmd;
            cmd.type = HBI_CMD_KERNEL_DISPATCH;
            cmd.params.dispatch.kernel_descriptor = kernel;
            cmd.params.dispatch.kernel_params = (const void *)&task->node->params;

            /* Inputs and outputs already resolved above */

            cmd.params.dispatch.inputs = inputs;
            cmd.params.dispatch.num_inputs = task->node->num_inputs;
            cmd.params.dispatch.outputs = outputs;
            cmd.params.dispatch.num_outputs = task->node->num_outputs;

            cmd.params.dispatch.workspace = hbi_kernel_workspace_ptr(&exec_ctx->workspace);
            cmd.params.dispatch.workspace_size = task->required_workspace;

            if (!backend->execute) {
                return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0,
                                   "executor_run: backend has no execute()");
            }
            st = backend->execute(ctx, &cmd);
            if (st != HBI_OK)
                return st;
        }
    }

    /* Flush any asynchronous work. */
    if (backend->sync) {
        st = backend->sync(ctx);
        if (st != HBI_OK)
            return st;
    }

    return HBI_OK;
}
