/* backend_cpu.c — the always-present reference backend.
 *
 * The CPU backend is the correctness baseline: it is always compiled, always
 * available, and every other backend falls back to it per-tensor on any failure
 * (DD-007). This implements the full Backend Interface (RFC-008) synchronously.
 */
#include "backend/backend.h"

#include "backend_cpu.h"
#include "backend_cpu_kernels.h"
#include "kernel/kernel.h"

#include <string.h>

/* ── Context & Statistics ────────────────────────────────────────────────── */

struct hbi_backend_context {
    hbi_allocator *allocator;
    hbi_backend_statistics stats;

    /* Conceptually represents the CPUCommandQueue, CPUKernelDispatcher,
       and CPUSynchronization mentioned in RFC-009.
       Currently, execution is purely synchronous on the calling thread. */
    bool is_active;
};

/* ── Backend VTable Implementations ──────────────────────────────────────── */

static hbi_status cpu_init(void) {
    /* Register the reference kernels (RFC-003, DD-025) so dispatch can find them. */
    return hb_backend_cpu_register_kernels();
}

static void cpu_shutdown(void) {
    /* Nothing to release globally in the CPU backend scaffold. */
}

static hbi_status cpu_get_capabilities(hbi_backend_capabilities *out_caps) {
    if (!out_caps)
        return HBI_ERR_INVALID_ARG;

    out_caps->supported_devices = HBI_DEVICE_TYPE_CPU;

    /* The CPU backend supports FP32 and INT8 (as registered by reference kernels) */
    out_caps->supported_datatypes = HBI_BACKEND_DTYPE_F32 | HBI_BACKEND_DTYPE_I8;

    /* Generous defaults for system memory */
    out_caps->max_memory_bytes = (size_t)-1;
    out_caps->max_workspace_bytes = (size_t)-1;
    out_caps->required_alignment = 64; /* Typical cacheline size */

    out_caps->supports_async_execution = false;
    out_caps->supports_sync_events = false; /* Since everything is synchronous */

    return HBI_OK;
}

static hbi_status cpu_create_context(hbi_allocator *allocator, hbi_backend_context **out_ctx) {
    if (!allocator || !out_ctx)
        return HBI_ERR_INVALID_ARG;

    hbi_backend_context *ctx = (hbi_backend_context *)hbi_alloc(
        allocator, sizeof(hbi_backend_context), 64, HBI_MEM_GENERAL);
    if (!ctx) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "Failed to allocate CPU context");
    }

    ctx->allocator = allocator;
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    ctx->is_active = true;

    *out_ctx = ctx;
    return HBI_OK;
}

static void cpu_destroy_context(hbi_backend_context *ctx) {
    if (ctx) {
        ctx->is_active = false;
        hbi_free(ctx->allocator, ctx);
    }
}

static hbi_status cpu_execute(hbi_backend_context *ctx, const hbi_backend_command *cmd) {
    if (!ctx || !cmd)
        return HBI_ERR_INVALID_ARG;
    if (!ctx->is_active)
        return HBI_ERR_STATE;

    switch (cmd->type) {
    case HBI_CMD_KERNEL_DISPATCH: {
        if (!cmd->params.dispatch.kernel_descriptor) {
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "CPU dispatch: NULL kernel descriptor");
        }
        const hbi_kernel *kernel = (const hbi_kernel *)cmd->params.dispatch.kernel_descriptor;

        /* Construct the args */
        hbi_kernel_args args;
        hbi_status st = hbi_kernel_args_init(&args);
        if (st != HBI_OK)
            return st;

        args.num_inputs = cmd->params.dispatch.num_inputs;
        for (uint32_t i = 0; i < args.num_inputs && i < HBI_KERNEL_MAX_INPUTS; ++i) {
            args.inputs[i] = (const hbi_tensor *)cmd->params.dispatch.inputs[i];
        }

        args.num_outputs = cmd->params.dispatch.num_outputs;
        for (uint32_t i = 0; i < args.num_outputs && i < HBI_KERNEL_MAX_OUTPUTS; ++i) {
            args.outputs[i] = (hbi_tensor *)cmd->params.dispatch.outputs[i];
        }

        /* Copy the opaque kernel params */
        if (cmd->params.dispatch.kernel_params) {
            args.params = *(const hbi_kernel_params *)cmd->params.dispatch.kernel_params;
        } else {
            memset(&args.params, 0, sizeof(args.params));
        }

        /* For now, we construct a dummy workspace if needed. */
        hbi_kernel_workspace ws;
        memset(&ws, 0, sizeof(ws));
        if (cmd->params.dispatch.workspace_size > 0 && cmd->params.dispatch.workspace) {
            ws.buffer = cmd->params.dispatch.workspace;
            ws.capacity = cmd->params.dispatch.workspace_size;
        }

        /* Run the kernel synchronously */
        st = kernel->run(&args, &ws);
        if (st == HBI_OK) {
            ctx->stats.kernels_dispatched++;
        }
        return st;
    }
    case HBI_CMD_MEMCOPY_H2D:
    case HBI_CMD_MEMCOPY_D2H:
    case HBI_CMD_MEMCOPY_D2D: {
        if (!cmd->params.copy.dst || !cmd->params.copy.src) {
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "CPU memcpy: NULL src or dst");
        }
        memcpy(cmd->params.copy.dst, cmd->params.copy.src, cmd->params.copy.bytes);

        if (cmd->type == HBI_CMD_MEMCOPY_H2D)
            ctx->stats.bytes_copied_host_to_device += cmd->params.copy.bytes;
        else if (cmd->type == HBI_CMD_MEMCOPY_D2H)
            ctx->stats.bytes_copied_device_to_host += cmd->params.copy.bytes;
        else
            ctx->stats.bytes_copied_device_to_device += cmd->params.copy.bytes;

        return HBI_OK;
    }
    case HBI_CMD_SYNC_BARRIER: {
        /* Synchronous execution means the barrier is immediately fulfilled. */
        return HBI_OK;
    }
    default:
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "CPU backend: unsupported command type %d",
                            cmd->type);
    }
}

static hbi_status cpu_sync(hbi_backend_context *ctx) {
    if (!ctx)
        return HBI_ERR_INVALID_ARG;
    if (!ctx->is_active)
        return HBI_ERR_STATE;
    /* Already synchronous. */
    return HBI_OK;
}

static hbi_status cpu_get_statistics(hbi_backend_context *ctx, hbi_backend_statistics *out_stats) {
    if (!ctx || !out_stats)
        return HBI_ERR_INVALID_ARG;
    if (!ctx->is_active)
        return HBI_ERR_STATE;

    *out_stats = ctx->stats;
    return HBI_OK;
}

static const hbi_backend g_cpu_backend = {.abi_version = HBI_BACKEND_ABI_VERSION,
                                          .name = "cpu",
                                          .init = cpu_init,
                                          .shutdown = cpu_shutdown,
                                          .get_capabilities = cpu_get_capabilities,
                                          .create_context = cpu_create_context,
                                          .destroy_context = cpu_destroy_context,
                                          .execute = cpu_execute,
                                          .sync = cpu_sync,
                                          .get_statistics = cpu_get_statistics};

hbi_status hb_backend_cpu_register(void) {
    return hbi_backend_register(&g_cpu_backend);
}
