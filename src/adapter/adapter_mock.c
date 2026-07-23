/* adapter_mock.c — Mock model adapter for testing the Adapter Framework (RFC-014).
 *
 * Implements the hbi_model_adapter vtable as a minimal "dense transformer"
 * adapter. Used exclusively by adapter_test.c; never registered in production.
 *
 * The mock adapter expects metadata with these keys:
 *   - "architecture"        = "mock"
 *   - "hidden_size"         = integer string (e.g. "256")
 *   - "num_attention_heads" = integer string (e.g. "4")
 *   - "num_layers"          = integer string (e.g. "2")
 *   - "vocab_size"          = integer string (e.g. "1024")
 */
#include "adapter/adapter_internal.h"

#include <stdlib.h>
#include <string.h>

/* ── Private mock state ────────────────────────────────────────────────── */

typedef struct mock_private {
    uint32_t hidden_size;
    uint32_t num_layers;
    uint32_t num_heads;
    uint32_t vocab_size;
    bool initialized;
} mock_private;

/* Global mock statistics (accumulated across calls). */
static hbi_model_statistics g_mock_stats;
static mock_private g_mock_state;

/* ── Helper: parse a uint32 from a metadata value ──────────────────────── */

static bool parse_uint32(const char *str, uint32_t *out) {
    if (!str || !out || str[0] == '\0') {
        return false;
    }
    char *end = NULL;
    long val = strtol(str, &end, 10);
    if (!end || *end != '\0' || val < 0) {
        return false;
    }
    *out = (uint32_t)val;
    return true;
}

/* ── Vtable callbacks ──────────────────────────────────────────────────── */

static hbi_status mock_init(const hbi_model_adapter *self, const hbi_load_session *session,
                            hbi_allocator *allocator) {
    HB_UNUSED(self);
    HB_UNUSED(allocator);

    memset(&g_mock_state, 0, sizeof(g_mock_state));
    memset(&g_mock_stats, 0, sizeof(g_mock_stats));

    if (!session) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "mock init: NULL session");
    }
    g_mock_state.initialized = true;
    return HBI_OK;
}

static hbi_status mock_validate_metadata(const hbi_model_adapter *self,
                                         const hbi_model_metadata *metadata) {
    HB_UNUSED(self);

    if (!metadata) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "mock validate: NULL metadata");
    }

    const char *arch = hbi_model_metadata_get(metadata, "architecture");
    if (!arch || strcmp(arch, "mock") != 0) {
        return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0,
                            "mock validate: expected architecture='mock', got '%s'",
                            arch ? arch : "(null)");
    }

    /* Required numeric keys. */
    static const char *const required_keys[] = {"hidden_size", "num_attention_heads", "num_layers",
                                                "vocab_size"};
    for (size_t i = 0; i < HB_ARRAY_LEN(required_keys); ++i) {
        const char *val = hbi_model_metadata_get(metadata, required_keys[i]);
        if (!val) {
            return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0, "mock validate: missing key '%s'",
                                required_keys[i]);
        }
        uint32_t dummy;
        if (!parse_uint32(val, &dummy)) {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "mock validate: '%s' is not a valid integer",
                                required_keys[i]);
        }
    }
    return HBI_OK;
}

static hbi_status mock_build_descriptor(const hbi_model_adapter *self,
                                        const hbi_model_metadata *metadata,
                                        hbi_model_descriptor *out) {
    HB_UNUSED(self);

    if (!metadata || !out) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "mock build_descriptor: NULL arg");
    }

    memset(out, 0, sizeof(*out));
    strncpy(out->architecture_name, "mock", HBI_ADAPTER_NAME_MAX - 1);
    out->architecture = HBI_ADAPTER_ARCH_TRANSFORMER_DENSE;

    parse_uint32(hbi_model_metadata_get(metadata, "hidden_size"), &out->hidden_size);
    parse_uint32(hbi_model_metadata_get(metadata, "num_attention_heads"),
                 &out->num_attention_heads);
    parse_uint32(hbi_model_metadata_get(metadata, "num_layers"), &out->num_layers);
    parse_uint32(hbi_model_metadata_get(metadata, "vocab_size"), &out->vocab_size);

    out->num_kv_heads = out->num_attention_heads; /* MHA */
    out->intermediate_size = out->hidden_size * 4;
    out->max_seq_length = 2048;

    out->attention_variant = HBI_ATTENTION_MHA;
    out->norm_type = HBI_NORM_RMSNORM;
    out->activation = HBI_ADAPTER_ACT_SILU;

    /* Dense transformer: embedding + attention + FFN + norm + output head. */
    hbi_descriptor_set_layer(out, HBI_LAYER_EMBEDDING);
    hbi_descriptor_set_layer(out, HBI_LAYER_ATTENTION);
    hbi_descriptor_set_layer(out, HBI_LAYER_FEED_FORWARD);
    hbi_descriptor_set_layer(out, HBI_LAYER_NORMALIZATION);
    hbi_descriptor_set_layer(out, HBI_LAYER_OUTPUT_HEAD);
    hbi_descriptor_set_layer(out, HBI_LAYER_RESIDUAL);

    g_mock_state.hidden_size = out->hidden_size;
    g_mock_state.num_layers = out->num_layers;
    g_mock_state.num_heads = out->num_attention_heads;
    g_mock_state.vocab_size = out->vocab_size;

    return HBI_OK;
}

static hbi_status mock_build_graph(const hbi_model_adapter *self, hbi_graph_builder *builder,
                                   const hbi_model_descriptor *descriptor) {
    HB_UNUSED(self);

    if (!builder || !descriptor) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "mock build_graph: NULL arg");
    }

    /* Build a minimal 2-node graph: input → copy → copy (embedding + norm stand-in).
     * COPY only needs 1 input and preserves shape, so no kernel params required. */

    /* Graph input: [batch=1, hidden_size]. */
    hbi_shape input_shape;
    memset(&input_shape, 0, sizeof(input_shape));
    input_shape.rank = 2;
    input_shape.dims[0] = 1;
    input_shape.dims[1] = (int64_t)descriptor->hidden_size;

    uint32_t input_id = 0;
    hbi_status st =
        hbi_graph_add_input(builder, "input_ids", &input_shape, HBI_DTYPE_FP32, &input_id);
    if (st != HBI_OK) {
        return st;
    }

    /* Node 1: "embedding" — a copy op standing in for the embedding lookup. */
    hbi_kernel_params params;
    memset(&params, 0, sizeof(params));

    uint32_t embed_out_id = 0;
    st = hbi_graph_add_node(builder, "mock_embedding", HBI_KERNEL_OP_COPY, &params, &input_id, 1,
                            &embed_out_id, 1);
    if (st != HBI_OK) {
        return st;
    }

    /* Node 2: "output_norm" — a copy op standing in for final normalization. */
    uint32_t norm_out_id = 0;
    st = hbi_graph_add_node(builder, "mock_norm", HBI_KERNEL_OP_COPY, &params, &embed_out_id, 1,
                            &norm_out_id, 1);
    if (st != HBI_OK) {
        return st;
    }

    g_mock_stats.graph_nodes = 2;
    g_mock_stats.graph_values = 3; /* input + embed_out + norm_out */

    return HBI_OK;
}

static hbi_status mock_register_tensors(const hbi_model_adapter *self,
                                        const hbi_model_manifest *manifest,
                                        const hbi_model_descriptor *descriptor) {
    HB_UNUSED(self);

    if (!manifest || !descriptor) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "mock register_tensors: NULL arg");
    }

    /* Check that the embedding tensor exists. */
    const hbi_tensor_entry *emb = hbi_model_manifest_find(manifest, "model.embed_tokens.weight");
    if (!emb) {
        return HBI_ERR_SET(HBI_ERR_NOT_FOUND, 0,
                           "mock register_tensors: missing 'model.embed_tokens.weight'");
    }

    g_mock_stats.tensors_registered = hbi_model_manifest_count(manifest);
    return HBI_OK;
}

static hbi_status mock_create_context(const hbi_model_adapter *self,
                                      const hbi_model_descriptor *descriptor,
                                      hbi_allocator *allocator, hbi_model_context **out) {
    HB_UNUSED(self);

    if (!descriptor || !allocator || !out) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "mock create_context: NULL arg");
    }
    *out = NULL;

    hbi_model_context *ctx =
        (hbi_model_context *)hbi_alloc(allocator, sizeof(hbi_model_context), 8, HBI_MEM_GENERAL);
    if (!ctx) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "mock create_context: alloc failed");
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->allocator = allocator;
    ctx->adapter = self;
    ctx->descriptor = *descriptor;
    ctx->stats = g_mock_stats;
    ctx->initialized = true;

    g_mock_stats.adapter_memory_bytes += sizeof(hbi_model_context);
    *out = ctx;
    return HBI_OK;
}

static void mock_destroy_context(const hbi_model_adapter *self, hbi_model_context *context) {
    HB_UNUSED(self);
    if (!context) {
        return;
    }
    hbi_allocator *a = context->allocator;
    context->initialized = false;
    hbi_free(a, context);
}

static uint32_t mock_get_capabilities(const hbi_model_adapter *self) {
    HB_UNUSED(self);
    return HBI_CAP_SUPPORTS_BATCHED_INFERENCE | HBI_CAP_SUPPORTS_QUANTIZED_WEIGHTS;
}

static hbi_status mock_get_statistics(const hbi_model_adapter *self, hbi_model_statistics *out) {
    HB_UNUSED(self);
    if (!out) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "mock get_statistics: NULL out");
    }
    *out = g_mock_stats;
    return HBI_OK;
}

static void mock_shutdown(const hbi_model_adapter *self) {
    HB_UNUSED(self);
    memset(&g_mock_state, 0, sizeof(g_mock_state));
    memset(&g_mock_stats, 0, sizeof(g_mock_stats));
}

/* ── Static adapter instance ───────────────────────────────────────────── */

static const hbi_model_adapter g_mock_adapter = {
    .name = "mock",
    .architecture = HBI_ADAPTER_ARCH_GENERIC,
    .init = mock_init,
    .validate_metadata = mock_validate_metadata,
    .build_descriptor = mock_build_descriptor,
    .build_graph = mock_build_graph,
    .register_tensors = mock_register_tensors,
    .create_context = mock_create_context,
    .destroy_context = mock_destroy_context,
    .get_capabilities = mock_get_capabilities,
    .get_statistics = mock_get_statistics,
    .shutdown = mock_shutdown,
};

/* ── Registration helper ───────────────────────────────────────────────── */

hbi_status hbi_adapter_mock_register(void) {
    return hbi_adapter_register(&g_mock_adapter);
}

/* Expose the mock adapter for direct use in tests. */
const hbi_model_adapter *hbi_adapter_mock_get(void) {
    return &g_mock_adapter;
}
