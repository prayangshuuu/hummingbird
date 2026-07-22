/* backend.c — Implementation of the Backend Interface & Compute Backend Framework */
#include "backend/backend_internal.h"
#include <string.h>

const char *hbi_backend_name(void) {
    return "backend";
}

enum { HBI_BACKEND_MAX = 8 };

static const hbi_backend *g_backends[HBI_BACKEND_MAX];
static int g_backend_count = 0;

/* ── Registry ────────────────────────────────────────────────────────────── */

hbi_status hbi_backend_register(const hbi_backend *backend) {
    if (!backend || !backend->name) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "Null backend or backend name");
    }
    if (backend->abi_version != HBI_BACKEND_ABI_VERSION) {
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0,
                            "Backend '%s' has incompatible ABI version %u (expected %u)",
                            backend->name, backend->abi_version, HBI_BACKEND_ABI_VERSION);
    }
    if (g_backend_count >= HBI_BACKEND_MAX) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "Backend registry is full");
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

const hbi_backend *hbi_backend_find(const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; i < g_backend_count; ++i) {
        if (strcmp(g_backends[i]->name, name) == 0) {
            return g_backends[i];
        }
    }
    return NULL;
}

/* ── Backend Manager ─────────────────────────────────────────────────────── */

hbi_status hbi_backend_manager_create(hbi_allocator *allocator, hbi_backend_manager **out_manager) {
    if (!allocator || !out_manager) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "Null allocator or out_manager");
    }
    hbi_backend_manager *mgr = (hbi_backend_manager *)hbi_alloc(
        allocator, sizeof(hbi_backend_manager), 8, HBI_MEM_GENERAL);
    if (!mgr) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "Failed to allocate backend manager");
    }

    mgr->allocator = allocator;
    for (int i = 0; i < HBI_BACKEND_MAX; ++i) {
        mgr->contexts[i] = NULL;
    }

    *out_manager = mgr;
    return HBI_OK;
}

void hbi_backend_manager_destroy(hbi_backend_manager *manager) {
    if (!manager)
        return;

    /* Clean up any active contexts we've created */
    for (int i = 0; i < g_backend_count; ++i) {
        if (manager->contexts[i] != NULL) {
            if (g_backends[i]->destroy_context) {
                g_backends[i]->destroy_context(manager->contexts[i]);
            }
            manager->contexts[i] = NULL;
        }
    }

    hbi_free(manager->allocator, manager);
}

hbi_status hbi_backend_manager_get_context(hbi_backend_manager *manager, const hbi_backend *backend,
                                           hbi_backend_context **out_ctx) {
    if (!manager || !backend || !out_ctx) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "Null arguments to manager_get_context");
    }

    /* Find backend index */
    int idx = -1;
    for (int i = 0; i < g_backend_count; ++i) {
        if (g_backends[i] == backend) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        return HBI_ERR_SET(HBI_ERR_NOT_FOUND, 0, "Backend not found in registry");
    }

    /* Create context if not already created */
    if (manager->contexts[idx] == NULL) {
        if (!backend->create_context) {
            return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "Backend does not support context creation");
        }
        hbi_status status = backend->create_context(manager->allocator, &manager->contexts[idx]);
        if (status != HBI_OK) {
            return status; /* error recorded by the backend */
        }
    }

    *out_ctx = manager->contexts[idx];
    return HBI_OK;
}

/* ── Selftest ────────────────────────────────────────────────────────────── */

hbi_status hbi_backend_selftest(void) {
    for (int i = 0; i < g_backend_count; ++i) {
        const hbi_backend *b = g_backends[i];
        if (!b || !b->name || b->abi_version != HBI_BACKEND_ABI_VERSION) {
            return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "Backend registry corrupted");
        }
    }
    return HBI_OK;
}
