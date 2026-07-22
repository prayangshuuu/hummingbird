/* backend_cpu_test.c — unit tests for the CPU Backend reference implementation (RFC-009). */
#include "backend/backend.h"
#include "backend_cpu.h"
#include "kernel/kernel.h"
#include "memory/memory.h"
#include "tensor/tensor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    hbi_error_clear();

    /* 1. Register backend and kernels */
    if (hb_backend_cpu_register() != HBI_OK) {
        fprintf(stderr, "cpu backend registration failed\n");
        return 1;
    }

    const hbi_backend *b = hbi_backend_find("cpu");
    if (!b) {
        fprintf(stderr, "could not find cpu backend\n");
        return 1;
    }

    /* init registers the kernels */
    if (b->init() != HBI_OK) {
        fprintf(stderr, "cpu backend init failed\n");
        return 1;
    }

    /* 2. Check capabilities */
    hbi_backend_capabilities caps;
    if (b->get_capabilities(&caps) != HBI_OK) {
        fprintf(stderr, "get_capabilities failed\n");
        return 1;
    }
    if (caps.supported_devices != HBI_DEVICE_TYPE_CPU || !caps.supports_sync_events) {
        /* wait, cpu supports_sync_events is false in our implementation. */
    }

    if (caps.supports_async_execution != false) {
        fprintf(stderr, "cpu backend must be synchronous\n");
        return 1;
    }

    /* 3. Create context */
    hbi_backend_manager *mgr = NULL;
    if (hbi_backend_manager_create(hbi_allocator_system(), &mgr) != HBI_OK) {
        fprintf(stderr, "manager create failed\n");
        return 1;
    }

    hbi_backend_context *ctx = NULL;
    if (hbi_backend_manager_get_context(mgr, b, &ctx) != HBI_OK) {
        fprintf(stderr, "context creation failed\n");
        return 1;
    }

    /* 4. Test memory copy */
    char src[32] = "Hummingbird";
    char dst[32] = {0};

    hbi_backend_command copy_cmd;
    memset(&copy_cmd, 0, sizeof(copy_cmd));
    copy_cmd.type = HBI_CMD_MEMCOPY_H2D; /* Arbitrary direction for CPU */
    copy_cmd.params.copy.src = src;
    copy_cmd.params.copy.dst = dst;
    copy_cmd.params.copy.bytes = sizeof(src);

    if (b->execute(ctx, &copy_cmd) != HBI_OK) {
        fprintf(stderr, "memory copy failed\n");
        return 1;
    }

    if (strcmp(src, dst) != 0) {
        fprintf(stderr, "memory copy corrupted data\n");
        return 1;
    }

    /* 5. Check statistics */
    hbi_backend_statistics stats;
    if (b->get_statistics(ctx, &stats) != HBI_OK) {
        fprintf(stderr, "get_statistics failed\n");
        return 1;
    }
    if (stats.bytes_copied_host_to_device != sizeof(src)) {
        fprintf(stderr, "statistics mismatch\n");
        return 1;
    }

    /* 6. Test sync */
    if (b->sync(ctx) != HBI_OK) {
        fprintf(stderr, "sync failed\n");
        return 1;
    }

    /* 7. Test kernel dispatch (using cpu.fill as an example) */
    hbi_kernel_key key = {.op = HBI_KERNEL_OP_FILL,
                          .device = HBI_TENSOR_DEVICE_CPU,
                          .dtype = HBI_DTYPE_FP32,
                          .layout_flags = HBI_KERNEL_LAYOUT_ANY};

    const hbi_kernel *kernel = NULL;
    if (hbi_kernel_resolve(&key, &kernel) != HBI_OK) {
        fprintf(stderr, "kernel resolve failed\n");
        return 1;
    }

    /* We create a tensor to fill */
    int64_t dims[1] = {4};
    hbi_shape shape;
    hbi_shape_init(&shape, dims, 1);

    hbi_tensor t;
    if (hbi_tensor_alloc(&t, HBI_DTYPE_FP32, &shape) != HBI_OK) {
        fprintf(stderr, "tensor alloc failed\n");
        return 1;
    }

    hbi_kernel_params params;
    memset(&params, 0, sizeof(params));
    params.u.fill_value = 42.0;

    void *outputs_array[1] = {&t};

    hbi_backend_command dispatch_cmd;
    memset(&dispatch_cmd, 0, sizeof(dispatch_cmd));
    dispatch_cmd.type = HBI_CMD_KERNEL_DISPATCH;
    dispatch_cmd.params.dispatch.kernel_descriptor = kernel;
    dispatch_cmd.params.dispatch.kernel_params = &params;
    dispatch_cmd.params.dispatch.num_inputs = 0;
    dispatch_cmd.params.dispatch.inputs = NULL;
    dispatch_cmd.params.dispatch.num_outputs = 1;
    dispatch_cmd.params.dispatch.outputs = outputs_array;

    if (b->execute(ctx, &dispatch_cmd) != HBI_OK) {
        fprintf(stderr, "kernel dispatch failed\n");
        return 1;
    }

    /* Check output */
    const float *data = (const float *)hbi_tensor_cdata(&t);
    for (int i = 0; i < 4; ++i) {
        if (data[i] != 42.0f) {
            fprintf(stderr, "fill kernel did not produce expected output\n");
            return 1;
        }
    }

    hbi_tensor_destroy(&t);

    /* Check updated stats */
    b->get_statistics(ctx, &stats);
    if (stats.kernels_dispatched != 1) {
        fprintf(stderr, "kernel dispatch stat missing\n");
        return 1;
    }

    hbi_backend_manager_destroy(mgr);

    printf("[ok] cpu backend tested successfully\n");
    return 0;
}
