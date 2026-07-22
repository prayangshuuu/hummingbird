/* backend_test.c — Unit tests for the Backend Interface & Compute Backend Framework */
#include "backend/backend.h"
#include "memory/memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mock Allocator */
static void *mock_alloc(void *user_data, size_t size, size_t alignment, hbi_mem_tag tag) {
    HB_UNUSED(user_data);
    HB_UNUSED(alignment);
    HB_UNUSED(tag);
    return malloc(size);
}
static void *mock_realloc(void *user_data, void *ptr, size_t new_size, size_t alignment,
                          hbi_mem_tag tag) {
    HB_UNUSED(user_data);
    HB_UNUSED(alignment);
    HB_UNUSED(tag);
    return realloc(ptr, new_size);
}
static void mock_free(void *user_data, void *ptr) {
    HB_UNUSED(user_data);
    free(ptr);
}
static hbi_allocator_vtable g_mock_vt = {mock_alloc, mock_realloc, mock_free, "mock_alloc"};
static hbi_allocator g_mock_allocator = {&g_mock_vt, NULL};

/* Mock Backend Context */
struct hbi_backend_context {
    int id;
    int execute_called;
    int sync_called;
};

/* Mock Backend Lifecycle */
static hbi_status mock_init(void) {
    return HBI_OK;
}
static void mock_shutdown(void) {
}

static hbi_status mock_get_capabilities(hbi_backend_capabilities *out_caps) {
    out_caps->supported_devices = HBI_DEVICE_TYPE_CPU;
    out_caps->supported_datatypes = HBI_DTYPE_F32 | HBI_DTYPE_F16;
    out_caps->max_memory_bytes = 1024 * 1024 * 1024;
    out_caps->max_workspace_bytes = 1024 * 1024 * 1024;
    out_caps->required_alignment = 64;
    out_caps->supports_async_execution = false;
    out_caps->supports_sync_events = true;
    return HBI_OK;
}

static hbi_status mock_create_context(hbi_allocator *allocator, hbi_backend_context **out_ctx) {
    hbi_backend_context *ctx = (hbi_backend_context *)hbi_alloc(
        allocator, sizeof(hbi_backend_context), 8, HBI_MEM_GENERAL);
    if (!ctx)
        return HBI_ERR_OOM;
    ctx->id = 42;
    ctx->execute_called = 0;
    ctx->sync_called = 0;
    *out_ctx = ctx;
    return HBI_OK;
}

static void mock_destroy_context(hbi_backend_context *ctx) {
    /* Since we allocated with hbi_alloc(g_mock_allocator), we technically should free it
     * with the allocator. Since this test controls everything, we can just free it directly. */
    free(ctx);
}

static hbi_status mock_execute(hbi_backend_context *ctx, const hbi_backend_command *cmd) {
    if (!ctx || !cmd)
        return HBI_ERR_INVALID_ARG;
    ctx->execute_called++;
    return HBI_OK;
}

static hbi_status mock_sync(hbi_backend_context *ctx) {
    if (!ctx)
        return HBI_ERR_INVALID_ARG;
    ctx->sync_called++;
    return HBI_OK;
}

static hbi_status mock_get_statistics(hbi_backend_context *ctx, hbi_backend_statistics *out_stats) {
    HB_UNUSED(ctx);
    memset(out_stats, 0, sizeof(*out_stats));
    return HBI_OK;
}

static hbi_backend g_mock_backend = {HBI_BACKEND_ABI_VERSION,
                                     "mock_cpu",
                                     mock_init,
                                     mock_shutdown,
                                     mock_get_capabilities,
                                     mock_create_context,
                                     mock_destroy_context,
                                     mock_execute,
                                     mock_sync,
                                     mock_get_statistics};

int main(void) {
    hbi_error_clear();

    if (hbi_backend_selftest() != HBI_OK) {
        fprintf(stderr, "%s: initial selftest failed\n", hbi_backend_name());
        return 1;
    }

    /* Test Registration */
    hbi_status status = hbi_backend_register(&g_mock_backend);
    if (status != HBI_OK) {
        fprintf(stderr, "Failed to register backend: %s\n", hbi_status_str(status));
        return 1;
    }

    if (hbi_backend_count() != 1) {
        fprintf(stderr, "Backend count %d != 1\n", hbi_backend_count());
        return 1;
    }

    const hbi_backend *found = hbi_backend_find("mock_cpu");
    if (found != &g_mock_backend) {
        fprintf(stderr, "Failed to find backend by name\n");
        return 1;
    }

    /* Test Capabilities */
    hbi_backend_capabilities caps;
    if (found->get_capabilities(&caps) != HBI_OK) {
        fprintf(stderr, "Failed to get capabilities\n");
        return 1;
    }
    if (caps.supported_devices != HBI_DEVICE_TYPE_CPU ||
        !(caps.supported_datatypes & HBI_DTYPE_F32)) {
        fprintf(stderr, "Capabilities mismatch\n");
        return 1;
    }

    /* Test Manager & Context Lifecycle */
    hbi_backend_manager *manager = NULL;
    status = hbi_backend_manager_create(&g_mock_allocator, &manager);
    if (status != HBI_OK || manager == NULL) {
        fprintf(stderr, "Failed to create manager\n");
        return 1;
    }

    hbi_backend_context *ctx = NULL;
    status = hbi_backend_manager_get_context(manager, found, &ctx);
    if (status != HBI_OK || ctx == NULL) {
        fprintf(stderr, "Failed to get context from manager: %s\n", hbi_status_str(status));
        return 1;
    }

    if (ctx->id != 42) {
        fprintf(stderr, "Context ID mismatch\n");
        return 1;
    }

    /* Test Command Execution */
    hbi_backend_command cmd;
    cmd.type = HBI_CMD_SYNC_BARRIER;
    status = found->execute(ctx, &cmd);
    if (status != HBI_OK || ctx->execute_called != 1) {
        fprintf(stderr, "Command execution failed\n");
        return 1;
    }

    status = found->sync(ctx);
    if (status != HBI_OK || ctx->sync_called != 1) {
        fprintf(stderr, "Synchronization failed\n");
        return 1;
    }

    /* Test Cleanup */
    hbi_backend_manager_destroy(manager); /* Should automatically destroy ctx */

    printf("[ok] %s\n", hbi_backend_name());
    return 0;
}
