/* backend.h — Stable extern-C backend ABI (CPU reference + optional CUDA/Metal) with per-tensor CPU
 * fallback.
 *
 * Core-public header for the `backend` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_BACKEND_H
#define HB_BACKEND_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Backend ABI (DD-007) ────────────────────────────────────────────────
 * A backend is a vtable of function pointers plus an ABI version. Backends
 * register at init; the runtime selects one and always keeps CPU as the
 * guaranteed fallback. Kept deliberately tiny in the bootstrap — compute
 * entry points (matmul, fused expert group, attention) are appended as the
 * tensor/executor contracts firm up.
 *
 * Bump HBI_BACKEND_ABI_VERSION on any incompatible change to hbi_backend.
 */
#define HBI_BACKEND_ABI_VERSION 1u

typedef struct hbi_backend {
    uint32_t abi_version; /* must equal HBI_BACKEND_ABI_VERSION       */
    const char *name;     /* stable identifier, e.g. "cpu", "cuda"   */
    /* Lifecycle. Return HBI_OK on success; core treats non-OK as "unavailable". */
    hbi_status (*init)(void);
    void (*shutdown)(void);
    /* (scaffold) compute entry points appended here as they are designed. */
} hbi_backend;

/* Register a backend vtable. Returns HBI_ERR_INVALID_ARG on a version or
 * shape mismatch, HBI_ERR_STATE if the registry is full. Registration does
 * not initialize the backend — the runtime selects and inits later.
 * Precondition: called at init time, before worker threads start (no locking). */
hbi_status hbi_backend_register(const hbi_backend *backend);

/* Number of currently registered backends. */
int hbi_backend_count(void);

/* Registered backend at [index], or NULL if out of range. */
const hbi_backend *hbi_backend_at(int index);

/* Human-readable module name. Never NULL. */
const char *hbi_backend_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_backend_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_BACKEND_H */
