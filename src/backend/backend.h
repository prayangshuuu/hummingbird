/* backend.h — Backend Interface & Compute Backend Framework (RFC-008)
 *
 * Core-public header for the `backend` module. This module defines the stable
 * contract between the Scheduler, Kernel Runtime, Device Manager, and every
 * compute backend (CPU, CUDA, Metal, etc.). The interface is minimal, fast,
 * and backend-independent.
 *
 * Symbols are prefixed `hbi_` (internal, no stability guarantee).
 * See docs/architecture.
 */
#ifndef HB_BACKEND_H
#define HB_BACKEND_H

#include "common/common.h"
#include "device/device.h"
#include "memory/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ABI & Types ─────────────────────────────────────────────────────────── */

#define HBI_BACKEND_ABI_VERSION 2u

/* Opaque handles */
typedef struct hbi_backend hbi_backend;
typedef struct hbi_backend_manager hbi_backend_manager;
typedef struct hbi_backend_context hbi_backend_context;

/* ── Capability Model ────────────────────────────────────────────────────── */

/* Data type support bitmask (simplified for the interface) */
typedef enum hbi_datatype_flags {
    HBI_BACKEND_DTYPE_F32 = (1 << 0),
    HBI_BACKEND_DTYPE_F16 = (1 << 1),
    HBI_BACKEND_DTYPE_BF16 = (1 << 2),
    HBI_BACKEND_DTYPE_I8 = (1 << 3),
    HBI_BACKEND_DTYPE_I4 = (1 << 4)
} hbi_datatype_flags;

typedef struct hbi_backend_capabilities {
    hbi_device_type supported_devices;
    uint32_t supported_datatypes; /* Bitwise OR of hbi_datatype_flags */

    size_t max_memory_bytes;
    size_t max_workspace_bytes;
    size_t required_alignment;

    bool supports_async_execution;
    bool supports_sync_events;
} hbi_backend_capabilities;

/* ── Command Interface ───────────────────────────────────────────────────── */

typedef enum hbi_command_type {
    HBI_CMD_KERNEL_DISPATCH = 1,
    HBI_CMD_MEMCOPY_H2D,
    HBI_CMD_MEMCOPY_D2H,
    HBI_CMD_MEMCOPY_D2D,
    HBI_CMD_SYNC_BARRIER
} hbi_command_type;

/* Generic command representing backend execution */
typedef struct hbi_backend_command {
    hbi_command_type type;

    union {
        /* HBI_CMD_KERNEL_DISPATCH */
        struct {
            const void *kernel_descriptor; /* Typed operation */
            const void *kernel_params;     /* Opaque parameters for the kernel */
            const void **inputs;           /* Array of input tensors */
            uint32_t num_inputs;
            void **outputs; /* Array of output tensors */
            uint32_t num_outputs;
            void *workspace; /* Scratch memory */
            size_t workspace_size;
        } dispatch;

        /* HBI_CMD_MEMCOPY_* */
        struct {
            void *dst;
            const void *src;
            size_t bytes;
        } copy;
    } params;
} hbi_backend_command;

/* ── Statistics ──────────────────────────────────────────────────────────── */

typedef struct hbi_backend_statistics {
    uint64_t kernels_dispatched;
    uint64_t bytes_copied_host_to_device;
    uint64_t bytes_copied_device_to_host;
    uint64_t bytes_copied_device_to_device;
    uint64_t total_execution_time_ns;
} hbi_backend_statistics;

/* ── Backend VTable (Plugin Interface) ───────────────────────────────────── */

/* The contract implemented by every backend (e.g., CPU, CUDA, Metal).
 * The runtime interacts with backends strictly through this vtable. */
struct hbi_backend {
    uint32_t abi_version; /* must equal HBI_BACKEND_ABI_VERSION */
    const char *name;     /* stable identifier, e.g. "cpu", "cuda" */

    /* Lifecycle */
    hbi_status (*init)(void);
    void (*shutdown)(void);

    /* Capability discovery */
    hbi_status (*get_capabilities)(hbi_backend_capabilities *out_caps);

    /* Context management */
    hbi_status (*create_context)(hbi_allocator *allocator, hbi_backend_context **out_ctx);
    void (*destroy_context)(hbi_backend_context *ctx);

    /* Execution */
    hbi_status (*execute)(hbi_backend_context *ctx, const hbi_backend_command *cmd);
    hbi_status (*sync)(hbi_backend_context *ctx);

    /* Profiling */
    hbi_status (*get_statistics)(hbi_backend_context *ctx, hbi_backend_statistics *out_stats);
};

/* ── Registry ────────────────────────────────────────────────────────────── */

/* Register a backend vtable dynamically. */
hbi_status hbi_backend_register(const hbi_backend *backend);

/* Number of currently registered backends. */
int hbi_backend_count(void);

/* Retrieve registered backend at [index]. */
const hbi_backend *hbi_backend_at(int index);

/* Retrieve registered backend by name (e.g., "cpu"). */
const hbi_backend *hbi_backend_find(const char *name);

/* ── Backend Manager ─────────────────────────────────────────────────────── */

/* Create a backend manager instance. */
hbi_status hbi_backend_manager_create(hbi_allocator *allocator, hbi_backend_manager **out_manager);

/* Destroy a backend manager instance and all its active contexts. */
void hbi_backend_manager_destroy(hbi_backend_manager *manager);

/* Obtain an execution context for a specific backend from the manager.
 * The manager owns the context lifecycle. */
hbi_status hbi_backend_manager_get_context(hbi_backend_manager *manager, const hbi_backend *backend,
                                           hbi_backend_context **out_ctx);

/* ── Module Identity ─────────────────────────────────────────────────────── */

/* Human-readable module name. Never NULL. */
const char *hbi_backend_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_backend_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_BACKEND_H */
