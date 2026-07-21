/* backend.c — Stable extern-C backend ABI (CPU reference + optional CUDA/Metal) with per-tensor CPU
 * fallback. */
#include "backend/backend_internal.h"

const char *hbi_backend_name(void) {
    return "backend";
}

/* ── Registry ────────────────────────────────────────────────────────────────
 * A fixed-size table is enough for the bootstrap: there are only ever a handful
 * of compiled-in backends (cpu, cuda, metal). Dynamic loading (DD-007) will grow
 * this later. Registration is expected at init time, before any worker threads,
 * so no locking yet — documented as a precondition. */
enum { HBI_BACKEND_MAX = 8 };

static const hbi_backend *g_backends[HBI_BACKEND_MAX];
static int g_backend_count;

hbi_status hbi_backend_register(const hbi_backend *backend) {
    if (backend == NULL || backend->name == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    if (backend->abi_version != HBI_BACKEND_ABI_VERSION) {
        /* Refuse a backend built against a different ABI (DD-007). */
        return HBI_ERR_UNSUPPORTED;
    }
    if (g_backend_count >= HBI_BACKEND_MAX) {
        return HBI_ERR_STATE;
    }
    g_backends[g_backend_count++] = backend;
    return HBI_OK;
}

int hbi_backend_count(void) {
    return g_backend_count;
}

const hbi_backend *hbi_backend_at(int index) {
    if (index < 0 || index >= g_backend_count) {
        return NULL;
    }
    return g_backends[index];
}

hbi_status hbi_backend_selftest(void) {
    /* Every registered backend must carry the current ABI version and a name. */
    for (int i = 0; i < g_backend_count; ++i) {
        const hbi_backend *b = g_backends[i];
        if (b == NULL || b->name == NULL) {
            return HBI_ERR_INTERNAL;
        }
        if (b->abi_version != HBI_BACKEND_ABI_VERSION) {
            return HBI_ERR_INTERNAL;
        }
    }
    return HBI_OK;
}
