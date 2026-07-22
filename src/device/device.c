/* device.c — Device discovery and hardware abstraction layer (RFC-006)
 *
 * Implements the Device Manager registry and CPU device discovery using
 * the underlying OS platform shim.
 */
#include "device/device_internal.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

/* ── Compile-time SIMD level capabilities ──────────────────────────────────────
 * Detected purely from predefined compiler macros to represent what the binary
 * may legally execute. */
static hbi_device_capabilities detect_compiled_simd(void) {
    hbi_device_capabilities caps = HBI_CAP_NONE;
#if defined(__AVX512F__)
    caps |= (HBI_CAP_SSE2 | HBI_CAP_AVX2 | HBI_CAP_AVX512);
#elif defined(__AVX2__)
    caps |= (HBI_CAP_SSE2 | HBI_CAP_AVX2);
#elif defined(__SSE2__) || (defined(_M_X64)) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    caps |= HBI_CAP_SSE2;
#endif

#if defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
    caps |= HBI_CAP_NEON;
#endif

#if defined(__ARM_FEATURE_SVE)
    caps |= HBI_CAP_SVE;
#endif

#if defined(__F16C__)
    caps |= HBI_CAP_F16C;
#endif

#if defined(__AVX512BF16__)
    caps |= HBI_CAP_BF16;
#endif

#if defined(__AVX512VNNI__)
    caps |= HBI_CAP_VNNI;
#endif

    return caps;
}

/* ── CPU Device Discovery ─────────────────────────────────────────────────── */

hbi_status hbi_device_cpu_discover(hbi_device *device) {
    if (device == NULL)
        return HBI_ERR_INVALID_ARG;

    memset(device, 0, sizeof(*device));
    device->id = 0;
    device->type = HBI_DEVICE_TYPE_CPU;
    device->capabilities = detect_compiled_simd();

    hbi_cpu_info cpu;
    if (hbi_platform_selftest() == HBI_OK && hbi_cpu_query(&cpu) == HBI_OK) {
        device->info.logical_cores = cpu.logical_cores;
        device->info.physical_cores = cpu.physical_cores;
        device->info.cacheline_size = cpu.cacheline_size;
        snprintf(device->info.arch, sizeof(device->info.arch), "%s", cpu.arch);

        device->memory.total_bytes = cpu.page_size * 1024ULL * 1024ULL; /* Fallback mockup */
        device->memory.available_bytes = device->memory.total_bytes;
        device->memory.alignment = 64;
        device->memory.supports_huge_pages = false;
        device->memory.num_regions = 1;
        device->memory.regions[0].page_size = cpu.page_size;
        device->memory.regions[0].numa_node = 0;
        device->memory.regions[0].total_bytes = device->memory.total_bytes;
        device->memory.regions[0].available_bytes = device->memory.available_bytes;

        device->capabilities |= HBI_CAP_UMA; /* Default to UMA for now */
    } else {
        /* Fallbacks */
        device->info.logical_cores = 1;
        device->info.physical_cores = 1;
        device->info.cacheline_size = 64;
        snprintf(device->info.arch, sizeof(device->info.arch), "unknown");

        device->memory.total_bytes = 1024ULL * 1024ULL * 1024ULL; /* 1GB */
        device->memory.available_bytes = device->memory.total_bytes;
        device->memory.alignment = 64;
        device->memory.num_regions = 1;
        device->memory.regions[0].page_size = 4096;
        device->memory.regions[0].numa_node = -1;
    }

    snprintf(device->info.vendor, sizeof(device->info.vendor), "Generic");
    snprintf(device->info.name, sizeof(device->info.name), "Host CPU");

    return HBI_OK;
}

/* ── Device Manager API ──────────────────────────────────────────────────── */

hbi_status hbi_device_manager_create(hbi_device_manager **out_manager) {
    if (!out_manager)
        return HBI_ERR_INVALID_ARG;

    hbi_device_manager *mgr = hbi_aligned_alloc(64, sizeof(hbi_device_manager));
    if (!mgr)
        return HBI_ERR_OOM;

    memset(mgr, 0, sizeof(*mgr));

    /* Always discover the host CPU first */
    hbi_device *cpu_dev = &mgr->devices[mgr->num_devices++];
    hbi_status st = hbi_device_cpu_discover(cpu_dev);
    if (st != HBI_OK) {
        hbi_aligned_free(mgr);
        return st;
    }

    *out_manager = mgr;
    return HBI_OK;
}

void hbi_device_manager_destroy(hbi_device_manager *manager) {
    if (manager) {
        hbi_aligned_free(manager);
    }
}

uint32_t hbi_device_manager_get_device_count(const hbi_device_manager *manager) {
    return manager ? manager->num_devices : 0;
}

const hbi_device *hbi_device_manager_get_device(const hbi_device_manager *manager, uint32_t index) {
    if (!manager || index >= manager->num_devices)
        return NULL;
    return &manager->devices[index];
}

const hbi_device *hbi_device_manager_get_best(const hbi_device_manager *manager) {
    if (!manager || manager->num_devices == 0)
        return NULL;

    /* Selection Policy: Return the first GPU if available, else CPU.
       Since we only support CPU for now, return the CPU. */
    for (uint32_t i = 0; i < manager->num_devices; ++i) {
        if (manager->devices[i].type != HBI_DEVICE_TYPE_CPU) {
            return &manager->devices[i];
        }
    }

    /* Fallback to CPU */
    return &manager->devices[0];
}

/* ── Device Accessors ────────────────────────────────────────────────────── */

hbi_device_type hbi_device_get_type(const hbi_device *device) {
    return device ? device->type : HBI_DEVICE_TYPE_COUNT;
}

hbi_device_capabilities hbi_device_get_capabilities(const hbi_device *device) {
    return device ? device->capabilities : HBI_CAP_NONE;
}

hbi_status hbi_device_get_info(const hbi_device *device, hbi_device_info *out_info) {
    if (!device || !out_info)
        return HBI_ERR_INVALID_ARG;
    *out_info = device->info;
    return HBI_OK;
}

hbi_status hbi_device_get_memory(const hbi_device *device, hbi_device_memory *out_memory) {
    if (!device || !out_memory)
        return HBI_ERR_INVALID_ARG;
    *out_memory = device->memory;
    return HBI_OK;
}

hbi_status hbi_device_get_statistics(const hbi_device *device, hbi_device_statistics *out_stats) {
    if (!device || !out_stats)
        return HBI_ERR_INVALID_ARG;
    *out_stats = device->stats;
    return HBI_OK;
}

int hbi_device_describe(const hbi_device *device, char *buf, size_t cap) {
    if (!device || !buf || cap == 0)
        return 0;

    const char *type_str = "Unknown";
    switch (device->type) {
    case HBI_DEVICE_TYPE_CPU:
        type_str = "CPU";
        break;
    case HBI_DEVICE_TYPE_CUDA:
        type_str = "CUDA";
        break;
    case HBI_DEVICE_TYPE_METAL:
        type_str = "Metal";
        break;
    case HBI_DEVICE_TYPE_ROCM:
        type_str = "ROCm";
        break;
    case HBI_DEVICE_TYPE_VULKAN:
        type_str = "Vulkan";
        break;
    default:
        break;
    }

    return snprintf(buf, cap, "[%s] %s %s %s %dc/%dp", type_str, device->info.vendor,
                    device->info.name, device->info.arch, device->info.logical_cores,
                    device->info.physical_cores);
}

const char *hbi_device_module_name(void) {
    return "device";
}

hbi_status hbi_device_selftest(void) {
    hbi_device_manager *mgr = NULL;
    if (hbi_device_manager_create(&mgr) != HBI_OK)
        return HBI_ERR_INTERNAL;

    if (hbi_device_manager_get_device_count(mgr) == 0) {
        hbi_device_manager_destroy(mgr);
        return HBI_ERR_INTERNAL;
    }

    const hbi_device *best = hbi_device_manager_get_best(mgr);
    if (!best) {
        hbi_device_manager_destroy(mgr);
        return HBI_ERR_INTERNAL;
    }

    hbi_device_info info;
    if (hbi_device_get_info(best, &info) != HBI_OK) {
        hbi_device_manager_destroy(mgr);
        return HBI_ERR_INTERNAL;
    }

    if (info.logical_cores < 1 || info.physical_cores < 1) {
        hbi_device_manager_destroy(mgr);
        return HBI_ERR_INTERNAL;
    }

    hbi_device_manager_destroy(mgr);
    return HBI_OK;
}
