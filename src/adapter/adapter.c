/* adapter.c — Model Adapter Framework implementation (RFC-014).
 *
 * Adapter registry, lifecycle helpers, enum-to-string tables, and the
 * init-model pipeline. See adapter.h for the full API contract.
 */
#include "adapter/adapter_internal.h"

#include "platform/platform.h"

#include <string.h>

/* ── Enum string tables ────────────────────────────────────────────────── */

static const char *const k_layer_type_names[] = {
    "invalid", "embedding", "attention",     "feed_forward",
    "moe",     "residual",  "normalization", "output_head",
};

const char *hbi_layer_type_str(hbi_layer_type lt) {
    if (lt >= 0 && lt < HBI_LAYER_COUNT) {
        return k_layer_type_names[lt];
    }
    return "invalid";
}

static const char *const k_attention_variant_names[] = {
    "invalid", "mha", "gqa", "mqa", "mla",
};

const char *hbi_attention_variant_str(hbi_attention_variant v) {
    if (v >= 0 && v < HBI_ATTENTION_COUNT) {
        return k_attention_variant_names[v];
    }
    return "invalid";
}

static const char *const k_norm_type_names[] = {
    "invalid",
    "rmsnorm",
    "layernorm",
};

const char *hbi_norm_type_str(hbi_norm_type n) {
    if (n >= 0 && n < HBI_NORM_COUNT) {
        return k_norm_type_names[n];
    }
    return "invalid";
}

static const char *const k_activation_type_names[] = {
    "invalid", "gelu", "silu", "relu", "swiglu",
};

const char *hbi_adapter_activation_str(hbi_adapter_activation a) {
    if (a >= 0 && a < HBI_ADAPTER_ACT_COUNT) {
        return k_activation_type_names[a];
    }
    return "invalid";
}

static const char *const k_architecture_names[] = {
    "unknown",
    "generic",
    "transformer_dense",
    "transformer_moe",
};

const char *hbi_adapter_architecture_str(hbi_adapter_architecture arch) {
    if (arch >= 0 && arch < HBI_ADAPTER_ARCH_COUNT) {
        return k_architecture_names[arch];
    }
    return "unknown";
}

/* ── Adapter registry ──────────────────────────────────────────────────── */

static const hbi_model_adapter *g_adapters[HBI_ADAPTER_REGISTRY_MAX];
static int g_adapter_count = 0;

hbi_status hbi_adapter_register(const hbi_model_adapter *adapter) {
    if (!adapter || !adapter->name) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0,
                           "adapter register: NULL adapter or missing name");
    }
    /* Validate required vtable fields. */
    if (!adapter->init || !adapter->validate_metadata || !adapter->build_descriptor ||
        !adapter->build_graph || !adapter->register_tensors || !adapter->create_context ||
        !adapter->destroy_context || !adapter->get_capabilities || !adapter->get_statistics) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0,
                           "adapter register: missing required vtable fields");
    }
    if (g_adapter_count >= (int)HBI_ADAPTER_REGISTRY_MAX) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "adapter registry is full");
    }
    /* Check for duplicate name. */
    for (int i = 0; i < g_adapter_count; ++i) {
        if (strcmp(g_adapters[i]->name, adapter->name) == 0) {
            return HBI_ERR_SETF(HBI_ERR_STATE, 0, "adapter register: duplicate name '%s'",
                                adapter->name);
        }
    }
    g_adapters[g_adapter_count++] = adapter;
    return HBI_OK;
}

const hbi_model_adapter *hbi_adapter_find(const char *architecture_name) {
    if (!architecture_name) {
        return NULL;
    }
    for (int i = 0; i < g_adapter_count; ++i) {
        if (strcmp(g_adapters[i]->name, architecture_name) == 0) {
            return g_adapters[i];
        }
    }
    return NULL;
}

const hbi_model_adapter *hbi_adapter_find_by_arch(hbi_adapter_architecture arch) {
    for (int i = 0; i < g_adapter_count; ++i) {
        if (g_adapters[i]->architecture == arch) {
            return g_adapters[i];
        }
    }
    return NULL;
}

int hbi_adapter_count(void) {
    return g_adapter_count;
}

void hbi_adapter_registry_clear(void) {
    g_adapter_count = 0;
    memset(g_adapters, 0, sizeof(g_adapters));
}

/* ── Adapter lifecycle helpers ─────────────────────────────────────────── */

hbi_status hbi_adapter_resolve(const hbi_load_session *session,
                               const hbi_model_adapter **out_adapter) {
    if (!session || !out_adapter) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "adapter resolve: NULL arg");
    }
    *out_adapter = NULL;

    const hbi_model_metadata *md = hbi_load_session_metadata(session);
    if (!md) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "adapter resolve: session has no metadata");
    }

    const char *arch = hbi_model_metadata_get(md, "architecture");
    if (!arch || arch[0] == '\0') {
        return HBI_ERR_SET(HBI_ERR_NOT_FOUND, 0,
                           "adapter resolve: no 'architecture' key in metadata");
    }

    const hbi_model_adapter *adapter = hbi_adapter_find(arch);
    if (!adapter) {
        return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0,
                            "adapter resolve: no adapter for architecture '%s'", arch);
    }
    *out_adapter = adapter;
    return HBI_OK;
}

hbi_status hbi_adapter_init_model(const hbi_model_adapter *adapter, const hbi_load_session *session,
                                  hbi_allocator *allocator, hbi_model_descriptor *out_descriptor,
                                  hbi_model_statistics *out_stats) {
    if (!adapter || !session || !allocator || !out_descriptor) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "adapter init_model: NULL arg");
    }

    memset(out_descriptor, 0, sizeof(*out_descriptor));
    hbi_model_statistics stats;
    memset(&stats, 0, sizeof(stats));

    const hbi_model_metadata *md = hbi_load_session_metadata(session);
    const hbi_model_manifest *manifest = hbi_load_session_manifest(session);

    /* 1. Init. */
    uint64_t t0 = hbi_time_monotonic_ns();
    hbi_status st = adapter->init(adapter, session, allocator);
    stats.init_time_ns = hbi_time_monotonic_ns() - t0;
    if (st != HBI_OK) {
        if (out_stats) {
            *out_stats = stats;
        }
        return st;
    }

    /* 2. Validate metadata. */
    st = adapter->validate_metadata(adapter, md);
    if (st != HBI_OK) {
        if (out_stats) {
            *out_stats = stats;
        }
        return st;
    }

    /* 3. Build descriptor. */
    st = adapter->build_descriptor(adapter, md, out_descriptor);
    if (st != HBI_OK) {
        if (out_stats) {
            *out_stats = stats;
        }
        return st;
    }

    /* 4. Register/verify tensors. */
    st = adapter->register_tensors(adapter, manifest, out_descriptor);
    if (st != HBI_OK) {
        if (out_stats) {
            *out_stats = stats;
        }
        return st;
    }
    stats.tensors_registered = hbi_model_manifest_count(manifest);

    if (out_stats) {
        *out_stats = stats;
    }
    return HBI_OK;
}

/* ── Module identity / self-test ───────────────────────────────────────── */

const char *hbi_adapter_name(void) {
    return "adapter";
}

hbi_status hbi_adapter_selftest(void) {
    /* Verify string tables. */
    if (strcmp(hbi_layer_type_str(HBI_LAYER_ATTENTION), "attention") != 0) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "adapter selftest: layer_type str mismatch");
    }
    if (strcmp(hbi_attention_variant_str(HBI_ATTENTION_GQA), "gqa") != 0) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "adapter selftest: attention_variant str mismatch");
    }
    if (strcmp(hbi_norm_type_str(HBI_NORM_RMSNORM), "rmsnorm") != 0) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "adapter selftest: norm_type str mismatch");
    }
    if (strcmp(hbi_adapter_activation_str(HBI_ADAPTER_ACT_SILU), "silu") != 0) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "adapter selftest: activation_type str mismatch");
    }
    if (strcmp(hbi_adapter_architecture_str(HBI_ADAPTER_ARCH_TRANSFORMER_DENSE),
               "transformer_dense") != 0) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "adapter selftest: architecture str mismatch");
    }
    return HBI_OK;
}
