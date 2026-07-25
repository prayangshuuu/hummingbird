/* backend_cuda_kernels.cu — kernel registry and stub dispatcher. */
#include <cuda_runtime.h>
#include <stddef.h>

extern "C" {
#include "backend/backend.h"
#include "kernel/kernel.h"
#include "backend_cuda.h"
}

/* Dummy kernels for RMSNorm, Linear, MatMul, Attention, Elementwise */
static hbi_status cuda_dummy_run(const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    (void)args;
    (void)ws;
    return HBI_OK;
}

static hbi_status cuda_dummy_workspace_size(const hbi_kernel_args *args, size_t *out_bytes) {
    (void)args;
    if (out_bytes) *out_bytes = 0;
    return HBI_OK;
}

static hbi_dtype dtypes_f32[] = { HBI_DTYPE_FP32 };
static hbi_dtype dtypes_f16[] = { HBI_DTYPE_FP16 };

#define DECL_DUMMY_KERNEL(op_enum, name_str, dt_arr, num_dt) \
    { op_enum, name_str, HBI_TENSOR_DEVICE_RESERVED_1, dt_arr, num_dt, HBI_KERNEL_LAYOUT_ANY, cuda_dummy_workspace_size, cuda_dummy_run }

/* Note: we use HBI_TENSOR_DEVICE_RESERVED_1 until HBI_TENSOR_DEVICE_CUDA exists */

static hbi_kernel g_cuda_kernels[] = {
    DECL_DUMMY_KERNEL(HBI_KERNEL_OP_MATMUL, "cuda.matmul.fp32", dtypes_f32, 1),
    DECL_DUMMY_KERNEL(HBI_KERNEL_OP_MATMUL, "cuda.matmul.fp16", dtypes_f16, 1),
    DECL_DUMMY_KERNEL(HBI_KERNEL_OP_RMSNORM, "cuda.rmsnorm.fp32", dtypes_f32, 1),
    DECL_DUMMY_KERNEL(HBI_KERNEL_OP_ELEMENTWISE, "cuda.elementwise.fp32", dtypes_f32, 1),
    DECL_DUMMY_KERNEL(HBI_KERNEL_OP_ATTENTION, "cuda.attention.fp32", dtypes_f32, 1),
};

extern "C" hbi_status hb_backend_cuda_register_kernels(void) {
    hbi_status st = HBI_OK;
    int num_kernels = sizeof(g_cuda_kernels) / sizeof(g_cuda_kernels[0]);
    for (int i = 0; i < num_kernels; ++i) {
        st = hbi_kernel_register(&g_cuda_kernels[i]);
        if (st != HBI_OK) return st;
    }
    return HBI_OK;
}
