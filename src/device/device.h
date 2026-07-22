/* device.h — Device discovery and hardware abstraction layer (RFC-006)
 *
 * Provides a unified abstraction over all compute and memory devices (CPU,
 * CUDA, Metal, etc.). The Device Manager handles discovery, capability
 * detection, memory reporting, and device selection.
 *
 * Core-public header for the `device` module (layer 3). Symbols are prefixed
 * `hbi_` (internal, no stability guarantee).
 *
 * Thread-safety: Device Manager and devices are immutable after discovery
 * and registration. They are safe to query concurrently.
 */
#ifndef HB_DEVICE_H
#define HB_DEVICE_H

#include "common/common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Device Types & Capabilities ───────────────────────────────────────────── */

typedef enum hbi_device_type {
    HBI_DEVICE_TYPE_CPU = 0,
    HBI_DEVICE_TYPE_CUDA,
    HBI_DEVICE_TYPE_METAL,
    HBI_DEVICE_TYPE_ROCM,
    HBI_DEVICE_TYPE_VULKAN,
    HBI_DEVICE_TYPE_COUNT
} hbi_device_type;

/* Capability bitflags. A device reports its supported features. */
typedef uint64_t hbi_device_capabilities;
#define HBI_CAP_NONE 0x0000000000000000ULL
/* Instruction set capabilities */
#define HBI_CAP_SSE2 0x0000000000000001ULL
#define HBI_CAP_AVX2 0x0000000000000002ULL
#define HBI_CAP_AVX512 0x0000000000000004ULL
#define HBI_CAP_NEON 0x0000000000000008ULL
#define HBI_CAP_SVE 0x0000000000000010ULL
#define HBI_CAP_F16C 0x0000000000000020ULL
#define HBI_CAP_BF16 0x0000000000000040ULL
#define HBI_CAP_VNNI 0x0000000000000080ULL
/* Architecture capabilities */
#define HBI_CAP_UMA 0x0000000100000000ULL  /* Unified Memory Architecture */
#define HBI_CAP_NUMA 0x0000000200000000ULL /* Non-Uniform Memory Access aware */

/* ── Memory Reporting ──────────────────────────────────────────────────────── */

/* A contiguous memory region (e.g., a specific NUMA node or VRAM bank) */
typedef struct hbi_memory_region {
    size_t total_bytes;
    size_t available_bytes;
    int numa_node;    /* -1 if not applicable */
    size_t page_size; /* bytes */
} hbi_memory_region;

/* Total device memory summary */
typedef struct hbi_device_memory {
    size_t total_bytes;
    size_t available_bytes;
    size_t alignment; /* Required alignment for allocations */
    bool supports_huge_pages;

    uint32_t num_regions;
    hbi_memory_region regions[4]; /* Up to 4 regions (e.g., NUMA nodes) */
} hbi_device_memory;

/* ── Device Information ────────────────────────────────────────────────────── */

/* Detailed static information about the device hardware */
typedef struct hbi_device_info {
    char vendor[32]; /* "Intel", "AMD", "Apple", "NVIDIA", etc. */
    char name[64];   /* "Core i9-13900K", "M2 Max", "RTX 4090" */
    char arch[16];   /* "x86_64", "aarch64", "ampere", etc. */

    int logical_cores;     /* Schedulable hardware threads */
    int physical_cores;    /* Physical cores (== logical if unknown) */
    size_t cacheline_size; /* Bytes per cacheline */
} hbi_device_info;

/* Dynamic statistics for the device */
typedef struct hbi_device_statistics {
    size_t currently_allocated_bytes;
    size_t peak_allocated_bytes;
    uint32_t active_allocations;
} hbi_device_statistics;

/* ── Device & Device Manager ───────────────────────────────────────────────── */

/* Opaque handle to a device */
typedef struct hbi_device hbi_device;
/* Opaque handle to the device manager */
typedef struct hbi_device_manager hbi_device_manager;

/* Create a new device manager instance. Discovers default host CPU. */
hbi_status hbi_device_manager_create(hbi_device_manager **out_manager);
void hbi_device_manager_destroy(hbi_device_manager *manager);

/* Device query and selection */
uint32_t hbi_device_manager_get_device_count(const hbi_device_manager *manager);
const hbi_device *hbi_device_manager_get_device(const hbi_device_manager *manager, uint32_t index);
const hbi_device *hbi_device_manager_get_best(const hbi_device_manager *manager);

/* Device accessors */
hbi_device_type hbi_device_get_type(const hbi_device *device);
hbi_device_capabilities hbi_device_get_capabilities(const hbi_device *device);
hbi_status hbi_device_get_info(const hbi_device *device, hbi_device_info *out_info);
hbi_status hbi_device_get_memory(const hbi_device *device, hbi_device_memory *out_memory);
hbi_status hbi_device_get_statistics(const hbi_device *device, hbi_device_statistics *out_stats);
int hbi_device_describe(const hbi_device *device, char *buf, size_t cap);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_device_module_name(void);
hbi_status hbi_device_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_DEVICE_H */
