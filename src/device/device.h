/* device.h — Device discovery and capability description, independent of any
 * backend (§3.7 keeps backends separate; this is the neutral hardware picture a
 * backend or the memory manager consults). It answers "what can this host do?"
 * — CPU core counts, page/cacheline sizes, the SIMD instruction level the engine
 * was COMPILED for, architecture string — without ever calling an OS API itself
 * (it builds on the platform shim, DD dependency rules).
 *
 * Core-public header for the `device` module (layer 3). Symbols are prefixed
 * `hbi_` (internal, no stability guarantee); external embedders use
 * <hummingbird/hummingbird.h>.
 *
 * GPU enumeration is deliberately out of scope here: discovering CUDA/Metal
 * devices is a backend concern (backends are optional plug-ins). This module
 * covers the always-present host CPU/memory picture the correctness path needs.
 *
 * Thread-safety: the host report is computed once (lazily, race-tolerantly) and
 * thereafter read-only, so every function here is safe to call from any thread.
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

/* ── SIMD level ──────────────────────────────────────────────────────────────
 * The vector instruction family the CPU backend was COMPILED to use. This is a
 * compile-time fact (what the binary may legally execute), not a runtime probe
 * of the current CPU — the two can differ, and the CPU backend's runtime
 * dispatch is a separate, later concern (PROJECT_CONTEXT §2.7). Ordered roughly
 * by capability so callers can compare. */
typedef enum hbi_simd_level {
    HBI_SIMD_NONE = 0, /* scalar fallback only */
    HBI_SIMD_SSE2,
    HBI_SIMD_AVX2,
    HBI_SIMD_AVX512,
    HBI_SIMD_NEON,       /* ARM AdvSIMD */
    HBI_SIMD_LEVEL_COUNT /* sentinel */
} hbi_simd_level;

/* Stable lower-case spelling ("none", "avx2", "neon", ...). Never NULL. */
const char *hbi_simd_level_str(hbi_simd_level level);

/* The SIMD level this translation unit / binary was compiled with. Constant for
 * the life of the process. */
hbi_simd_level hbi_device_simd_level(void);

/* ── Host capability report ──────────────────────────────────────────────────
 * A flat, copyable snapshot of the host. `logical_cores` is what a thread pool
 * should size against by default; `physical_cores` guides SMT-aware tuning. */
typedef struct hbi_device_info {
    int logical_cores;     /* schedulable hardware threads (>= 1) */
    int physical_cores;    /* physical cores (== logical if unknown) */
    size_t page_size;      /* bytes */
    size_t cacheline_size; /* bytes */
    char arch[16];         /* "x86_64", "aarch64", ... */
    hbi_simd_level simd;   /* compiled SIMD level */
} hbi_device_info;

/* Fill *out with the host report. Returns HBI_ERR_INVALID_ARG if out is NULL;
 * otherwise HBI_OK (unknown fields carry safe defaults, never a failure). The
 * report is cached after first call. */
hbi_status hbi_device_query(hbi_device_info *out);

/* Convenience accessors (compute the report if needed). logical_cores is
 * clamped to >= 1 so callers can divide by it safely. */
int hbi_device_logical_cores(void);

/* Write a one-line human-readable summary into `buf` (always NUL-terminated when
 * cap>0), e.g. "x86_64 8c/4p page=4096 line=64 simd=avx2". Returns the length
 * that would be written (like snprintf), so truncation is detectable. */
int hbi_device_describe(char *buf, size_t cap);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_device_name(void);
hbi_status hbi_device_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_DEVICE_H */
