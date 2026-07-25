/* backend_cuda.cu — optional CUDA backend.
 *
 * Implements the full Backend Interface (RFC-016) for CUDA asynchronously.
 */
#include <cuda_runtime.h>
#include <string.h>

extern "C" {
#include "backend/backend.h"
#include "backend_cuda.h"
#include "kernel/kernel.h"
}

/* Error Translation */
static hbi_status translate_cuda_error(cudaError_t err, const char* file, int line, const char* func, const char* msg) {
    if (err == cudaSuccess) return HBI_OK;
    hbi_status status = HBI_ERR_INTERNAL;
    if (err == cudaErrorMemoryAllocation) status = HBI_ERR_OOM;
    else if (err == cudaErrorInvalidValue) status = HBI_ERR_INVALID_ARG;
    return hbi_error_set_at(status, err, file, line, func, msg);
}

#define CUDA_CHECK(expr, msg) \
    do { \
        cudaError_t _err = (expr); \
        if (_err != cudaSuccess) { \
            return translate_cuda_error(_err, __FILE__, __LINE__, __func__, msg); \
        } \
    } while(0)

/* Context */
struct hbi_backend_context {
    hbi_allocator *allocator;
    hbi_backend_statistics stats;
    cudaStream_t stream;
    bool is_active;
};

static hbi_status cuda_init(void) {
    int count = 0;
    cudaError_t err = cudaGetDeviceCount(&count);
    if (err != cudaSuccess || count == 0) return HBI_ERR_UNSUPPORTED;
    return hb_backend_cuda_register_kernels();
}

static void cuda_shutdown(void) {
}

static hbi_status cuda_get_capabilities(hbi_backend_capabilities *out_caps) {
    if (!out_caps) return HBI_ERR_INVALID_ARG;
    out_caps->supported_devices = HBI_DEVICE_TYPE_CUDA;
    out_caps->supported_datatypes = HBI_BACKEND_DTYPE_F32 | HBI_BACKEND_DTYPE_F16 | HBI_BACKEND_DTYPE_BF16 | HBI_BACKEND_DTYPE_I8 | HBI_BACKEND_DTYPE_I4;
    out_caps->max_memory_bytes = (size_t)-1;
    out_caps->max_workspace_bytes = (size_t)-1;
    out_caps->required_alignment = 256;
    out_caps->supports_async_execution = true;
    out_caps->supports_sync_events = true;
    return HBI_OK;
}

static hbi_status cuda_create_context(hbi_allocator *allocator, hbi_backend_context **out_ctx) {
    if (!allocator || !out_ctx) return HBI_ERR_INVALID_ARG;
    hbi_backend_context *ctx = (hbi_backend_context *)hbi_alloc(allocator, sizeof(hbi_backend_context), 64, HBI_MEM_GENERAL);
    if (!ctx) return HBI_ERR_SET(HBI_ERR_OOM, 0, "Failed to allocate CUDA context");
    
    CUDA_CHECK(cudaStreamCreate(&ctx->stream), "Failed to create CUDA stream");
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    ctx->allocator = allocator;
    ctx->is_active = true;
    *out_ctx = ctx;
    return HBI_OK;
}

static void cuda_destroy_context(hbi_backend_context *ctx) {
    if (ctx) {
        ctx->is_active = false;
        cudaStreamDestroy(ctx->stream);
        hbi_free(ctx->allocator, ctx);
    }
}

static hbi_status cuda_execute(hbi_backend_context *ctx, const hbi_backend_command *cmd) {
    if (!ctx || !cmd) return HBI_ERR_INVALID_ARG;
    if (!ctx->is_active) return HBI_ERR_STATE;
    
    switch (cmd->type) {
    case HBI_CMD_KERNEL_DISPATCH: {
        if (!cmd->params.dispatch.kernel_descriptor) return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "CUDA dispatch NULL kernel");
        const hbi_kernel *kernel = (const hbi_kernel *)cmd->params.dispatch.kernel_descriptor;
        hbi_kernel_args args;
        hbi_kernel_args_init(&args);
        args.num_inputs = cmd->params.dispatch.num_inputs;
        for (uint32_t i = 0; i < args.num_inputs; ++i) args.inputs[i] = (const hbi_tensor*)cmd->params.dispatch.inputs[i];
        args.num_outputs = cmd->params.dispatch.num_outputs;
        for (uint32_t i = 0; i < args.num_outputs; ++i) args.outputs[i] = (hbi_tensor*)cmd->params.dispatch.outputs[i];
        if (cmd->params.dispatch.kernel_params) args.params = *(const hbi_kernel_params *)cmd->params.dispatch.kernel_params;
        hbi_kernel_workspace ws;
        memset(&ws, 0, sizeof(ws));
        if (cmd->params.dispatch.workspace_size > 0 && cmd->params.dispatch.workspace) {
            ws.buffer = cmd->params.dispatch.workspace;
            ws.capacity = cmd->params.dispatch.workspace_size;
        }
        hbi_status st = kernel->run(&args, &ws);
        if (st == HBI_OK) ctx->stats.kernels_dispatched++;
        return st;
    }
    case HBI_CMD_MEMCOPY_H2D: {
        CUDA_CHECK(cudaMemcpyAsync(cmd->params.copy.dst, cmd->params.copy.src, cmd->params.copy.bytes, cudaMemcpyHostToDevice, ctx->stream), "H2D");
        ctx->stats.bytes_copied_host_to_device += cmd->params.copy.bytes;
        return HBI_OK;
    }
    case HBI_CMD_MEMCOPY_D2H: {
        CUDA_CHECK(cudaMemcpyAsync(cmd->params.copy.dst, cmd->params.copy.src, cmd->params.copy.bytes, cudaMemcpyDeviceToHost, ctx->stream), "D2H");
        ctx->stats.bytes_copied_device_to_host += cmd->params.copy.bytes;
        return HBI_OK;
    }
    case HBI_CMD_MEMCOPY_D2D: {
        CUDA_CHECK(cudaMemcpyAsync(cmd->params.copy.dst, cmd->params.copy.src, cmd->params.copy.bytes, cudaMemcpyDeviceToDevice, ctx->stream), "D2D");
        ctx->stats.bytes_copied_device_to_device += cmd->params.copy.bytes;
        return HBI_OK;
    }
    case HBI_CMD_SYNC_BARRIER: {
        CUDA_CHECK(cudaStreamSynchronize(ctx->stream), "Sync");
        return HBI_OK;
    }
    default:
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "CUDA backend: unsupported command type %d", (int)cmd->type);
    }
}

static hbi_status cuda_sync(hbi_backend_context *ctx) {
    if (!ctx) return HBI_ERR_INVALID_ARG;
    if (!ctx->is_active) return HBI_ERR_STATE;
    CUDA_CHECK(cudaStreamSynchronize(ctx->stream), "Sync Context");
    return HBI_OK;
}

static hbi_status cuda_get_statistics(hbi_backend_context *ctx, hbi_backend_statistics *out_stats) {
    if (!ctx || !out_stats) return HBI_ERR_INVALID_ARG;
    if (!ctx->is_active) return HBI_ERR_STATE;
    *out_stats = ctx->stats;
    return HBI_OK;
}

static const hbi_backend g_cuda_backend = {
    HBI_BACKEND_ABI_VERSION,
    "cuda",
    cuda_init,
    cuda_shutdown,
    cuda_get_capabilities,
    cuda_create_context,
    cuda_destroy_context,
    cuda_execute,
    cuda_sync,
    cuda_get_statistics
};

extern "C" hbi_status hb_backend_cuda_register(void) {
    return hbi_backend_register(&g_cuda_backend);
}
