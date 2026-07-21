/* device.c — host capability report, built on the platform shim.
 *
 * All OS access goes through hbi_cpu_query (platform, layer 1); this module only
 * interprets the numbers and adds the compile-time SIMD level. The report is
 * computed once and cached; recomputation is idempotent, so the lazy init is
 * race-tolerant (worst case two threads compute the same values into the same
 * fields before the ready flag is observed).
 */
#include "device/device_internal.h"

#include "platform/platform.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* ── Compile-time SIMD level ─────────────────────────────────────────────────
 * Determined purely from predefined compiler macros: what the binary may
 * legally execute. Runtime CPU-feature dispatch is a separate, later concern. */
static hbi_simd_level detect_compiled_simd(void) {
#if defined(__AVX512F__)
    return HBI_SIMD_AVX512;
#elif defined(__AVX2__)
    return HBI_SIMD_AVX2;
#elif defined(__SSE2__) || (defined(_M_X64)) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    return HBI_SIMD_SSE2;
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
    return HBI_SIMD_NEON;
#else
    return HBI_SIMD_NONE;
#endif
}

const char *hbi_simd_level_str(hbi_simd_level level) {
    switch (level) {
    case HBI_SIMD_NONE:
        return "none";
    case HBI_SIMD_SSE2:
        return "sse2";
    case HBI_SIMD_AVX2:
        return "avx2";
    case HBI_SIMD_AVX512:
        return "avx512";
    case HBI_SIMD_NEON:
        return "neon";
    case HBI_SIMD_LEVEL_COUNT:
        break;
    }
    return "unknown";
}

hbi_simd_level hbi_device_simd_level(void) {
    return detect_compiled_simd();
}

/* ── Cached host report ──────────────────────────────────────────────────────
 * `g_ready` gates use of g_info. Both writers compute identical values, so a
 * benign race only redoes work; acquire/release publishes the finished struct. */
static hbi_device_info g_info;
static atomic_int g_ready; /* 0 = not computed, 1 = computed */

static void compute_report(hbi_device_info *out) {
    hbi_cpu_info cpu;
    memset(out, 0, sizeof(*out));

    if (hbi_cpu_query(&cpu) == HBI_OK) {
        out->logical_cores = cpu.logical_cores;
        out->physical_cores = cpu.physical_cores;
        out->page_size = cpu.page_size;
        out->cacheline_size = cpu.cacheline_size;
        memcpy(out->arch, cpu.arch, sizeof(out->arch));
        out->arch[sizeof(out->arch) - 1] = '\0';
    } else {
        /* platform only fails on a NULL arg, which cannot happen here; keep safe
         * defaults regardless so the report is always well-formed. */
        out->logical_cores = 1;
        out->physical_cores = 1;
        out->page_size = 4096;
        out->cacheline_size = 64;
        memcpy(out->arch, "unknown", sizeof("unknown"));
    }
    out->simd = detect_compiled_simd();
}

static const hbi_device_info *cached_report(void) {
    if (atomic_load_explicit(&g_ready, memory_order_acquire) == 0) {
        hbi_device_info local;
        compute_report(&local);
        g_info = local;
        atomic_store_explicit(&g_ready, 1, memory_order_release);
    }
    return &g_info;
}

hbi_status hbi_device_query(hbi_device_info *out) {
    if (out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "device_query: out is NULL");
    }
    *out = *cached_report();
    return HBI_OK;
}

int hbi_device_logical_cores(void) {
    int n = cached_report()->logical_cores;
    return n < 1 ? 1 : n;
}

int hbi_device_describe(char *buf, size_t cap) {
    const hbi_device_info *info = cached_report();
    int n = snprintf(buf, cap, "%s %dc/%dp page=%zu line=%zu simd=%s", info->arch,
                     info->logical_cores, info->physical_cores, info->page_size,
                     info->cacheline_size, hbi_simd_level_str(info->simd));
    return n;
}

const char *hbi_device_name(void) {
    return "device";
}

hbi_status hbi_device_selftest(void) {
    hbi_device_info info;
    if (hbi_device_query(&info) != HBI_OK) {
        return HBI_ERR_INTERNAL;
    }
    /* Core counts must be sane and page/cacheline non-zero. */
    if (info.logical_cores < 1 || info.physical_cores < 1) {
        return HBI_ERR_INTERNAL;
    }
    if (info.page_size == 0 || info.cacheline_size == 0) {
        return HBI_ERR_INTERNAL;
    }
    if (info.arch[0] == '\0') {
        return HBI_ERR_INTERNAL;
    }
    /* Every SIMD level must have a non-empty spelling. */
    for (int i = 0; i < HBI_SIMD_LEVEL_COUNT; ++i) {
        const char *s = hbi_simd_level_str((hbi_simd_level)i);
        if (s == NULL || s[0] == '\0') {
            return HBI_ERR_INTERNAL;
        }
    }
    return HBI_OK;
}
