/* adapter.h — Model Adapter Framework: architecture-independent adapter interface,
 * dynamic registry, model descriptor, capabilities, and statistics (RFC-014).
 *
 * Core-public header for the `adapter` module (layer 6). Other modules include
 * this; external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee).
 *
 * ── Design (RFC-014) ─────────────────────────────────────────────────────────
 * The Model Adapter Framework translates model-specific architectures into
 * Hummingbird's generic execution runtime. The core runtime must never contain
 * architecture-specific logic. Instead, every supported model family provides an
 * adapter implementing a common vtable interface.
 *
 * Adapter lifecycle:
 *   1. Register adapters at init time (before workers)
 *   2. Load model via hbi_model_load() (model module)
 *   3. Find adapter by architecture name from metadata
 *   4. adapter->init(adapter, load_session, allocator) → adapter-private state
 *   5. adapter->validate_metadata(adapter, metadata) → HBI_OK or error
 *   6. adapter->build_graph(adapter, builder, descriptor) → populate graph
 *   7. adapter->register_tensors(adapter, manifest, descriptor) → verify tensors
 *   8. adapter->create_context(adapter, descriptor, allocator) → hbi_model_context
 *   9. Runtime uses the context + graph for inference
 *  10. adapter->destroy_context(adapter, context) on teardown
 *  11. adapter->shutdown(adapter) on engine shutdown
 *
 * ── Ownership ────────────────────────────────────────────────────────────────
 * The adapter vtable is a static const; the registry borrows pointers (never
 * copies). hbi_model_context is owned by the caller and destroyed explicitly.
 * The descriptor is filled by the adapter and borrowed by the runtime.
 *
 * ── Thread-safety ────────────────────────────────────────────────────────────
 * The adapter registry is populated at init time (before workers), like the
 * format-handler and kernel registries. A model context is not thread-safe;
 * serialize externally or use one per inference session.
 */
#ifndef HB_ADAPTER_H
#define HB_ADAPTER_H

#include "common/common.h"
#include "graph/graph.h"
#include "memory/memory.h"
#include "model/model.h"
#include "tensor/tensor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Architecture abstraction enums ─────────────────────────────────────── */

/* Generic layer/block concepts that span model families. An adapter declares
 * which layer types its architecture uses via the model descriptor.
 * Append-only (values are an internal contract; never renumber). */
typedef enum hbi_layer_type {
    HBI_LAYER_INVALID = 0,   /* zero-init guard */
    HBI_LAYER_EMBEDDING,     /* token embedding table */
    HBI_LAYER_ATTENTION,     /* self-attention / cross-attention block */
    HBI_LAYER_FEED_FORWARD,  /* dense MLP (gate/up/down) */
    HBI_LAYER_MOE,           /* Mixture-of-Experts routing block */
    HBI_LAYER_RESIDUAL,      /* residual (skip) connection marker */
    HBI_LAYER_NORMALIZATION, /* pre/post normalization */
    HBI_LAYER_OUTPUT_HEAD,   /* lm_head / output projection */
    HBI_LAYER_COUNT          /* sentinel (not a layer type) */
} hbi_layer_type;

/* Stable lower-case spelling. Never NULL; "invalid" for out-of-range. */
const char *hbi_layer_type_str(hbi_layer_type lt);

/* Attention mechanism variant. Selectable per adapter; the runtime dispatches
 * to the appropriate op module based on this. */
typedef enum hbi_attention_variant {
    HBI_ATTENTION_INVALID = 0,
    HBI_ATTENTION_MHA,  /* Multi-Head Attention (Vaswani et al.) */
    HBI_ATTENTION_GQA,  /* Grouped-Query Attention (Ainslie et al.) */
    HBI_ATTENTION_MQA,  /* Multi-Query Attention (Shazeer) */
    HBI_ATTENTION_MLA,  /* Multi-head Latent Attention (DeepSeek) */
    HBI_ATTENTION_COUNT /* sentinel */
} hbi_attention_variant;

const char *hbi_attention_variant_str(hbi_attention_variant v);

/* Normalization variant. */
typedef enum hbi_norm_type {
    HBI_NORM_INVALID = 0,
    HBI_NORM_RMSNORM,   /* Root Mean Square Layer Normalization */
    HBI_NORM_LAYERNORM, /* Standard Layer Normalization */
    HBI_NORM_COUNT      /* sentinel */
} hbi_norm_type;

const char *hbi_norm_type_str(hbi_norm_type n);

/* Activation function used in feed-forward blocks. This is the adapter-level
 * description; the kernel module has its own `hbi_activation_kind` for dispatch.
 * Prefixed HBI_ADAPTER_ACT_ to avoid collision with kernel.h's enumerators. */
typedef enum hbi_adapter_activation {
    HBI_ADAPTER_ACT_INVALID = 0,
    HBI_ADAPTER_ACT_GELU,
    HBI_ADAPTER_ACT_SILU,
    HBI_ADAPTER_ACT_RELU,
    HBI_ADAPTER_ACT_SWIGLU, /* SwiGLU (Shazeer) — fused silu+gate */
    HBI_ADAPTER_ACT_COUNT   /* sentinel */
} hbi_adapter_activation;

const char *hbi_adapter_activation_str(hbi_adapter_activation a);

/* High-level architecture family. Used for registry lookup; the adapter itself
 * knows the detailed geometry via the model descriptor. */
typedef enum hbi_adapter_architecture {
    HBI_ADAPTER_ARCH_UNKNOWN = 0,
    HBI_ADAPTER_ARCH_GENERIC, /* architecture-agnostic / test mock */
    HBI_ADAPTER_ARCH_TRANSFORMER_DENSE,
    HBI_ADAPTER_ARCH_TRANSFORMER_MOE,
    HBI_ADAPTER_ARCH_COUNT /* sentinel */
} hbi_adapter_architecture;

const char *hbi_adapter_architecture_str(hbi_adapter_architecture arch);

/* ── Model descriptor ────────────────────────────────────────────────────── */

#define HBI_ADAPTER_NAME_MAX 64u
#define HBI_ADAPTER_MAX_LAYERS 512u

/* Declarative description of a model's geometry, produced by the adapter after
 * inspecting the load session's metadata. The runtime uses this to size
 * buffers, plan execution, and configure the KV cache — without knowing the
 * model's architecture directly. */
typedef struct hbi_model_descriptor {
    char architecture_name[HBI_ADAPTER_NAME_MAX]; /* e.g. "llama", "glm", "gpt-oss" */
    hbi_adapter_architecture architecture;        /* high-level family */

    /* Transformer geometry. */
    uint32_t num_layers;          /* number of transformer blocks */
    uint32_t hidden_size;         /* model hidden dimension */
    uint32_t num_attention_heads; /* query heads per layer */
    uint32_t num_kv_heads;        /* key/value heads (== num_heads for MHA) */
    uint32_t intermediate_size;   /* FFN intermediate dim */
    uint32_t vocab_size;          /* token vocabulary size */
    uint32_t max_seq_length;      /* maximum supported sequence length */

    /* MoE parameters (0 for dense models). */
    uint32_t num_experts;        /* total experts per MoE layer */
    uint32_t num_experts_active; /* top-K active experts per token */
    uint32_t moe_layer_stride;   /* MoE every N layers (0 = none) */

    /* Variants. */
    hbi_attention_variant attention_variant;
    hbi_norm_type norm_type;
    hbi_adapter_activation activation;

    /* Layer-type mask: which HBI_LAYER_* types this architecture uses.
     * Stored as a bitfield so the runtime can query without a list walk. */
    uint32_t layer_type_mask;
} hbi_model_descriptor;

/* Set a bit in the layer-type mask for the given layer type. */
static inline void hbi_descriptor_set_layer(hbi_model_descriptor *desc, hbi_layer_type lt) {
    if (desc && lt > HBI_LAYER_INVALID && lt < HBI_LAYER_COUNT) {
        desc->layer_type_mask |= (1u << (uint32_t)lt);
    }
}

/* Query whether the descriptor declares a given layer type. */
static inline bool hbi_descriptor_has_layer(const hbi_model_descriptor *desc, hbi_layer_type lt) {
    if (!desc || lt <= HBI_LAYER_INVALID || lt >= HBI_LAYER_COUNT) {
        return false;
    }
    return (desc->layer_type_mask & (1u << (uint32_t)lt)) != 0;
}

/* ── Model capabilities ─────────────────────────────────────────────────── */

/* Bit flags describing what an adapter/model supports. The runtime queries
 * these to enable or disable features without hard-coding architecture checks. */
typedef enum hbi_model_capability {
    HBI_CAP_NONE = 0u,
    HBI_CAP_SUPPORTS_SPARSE_MOE = (1u << 0),
    HBI_CAP_SUPPORTS_KV_COMPRESSION = (1u << 1),
    HBI_CAP_SUPPORTS_SPECULATIVE_DECODING = (1u << 2),
    HBI_CAP_SUPPORTS_BATCHED_INFERENCE = (1u << 3),
    HBI_CAP_SUPPORTS_QUANTIZED_WEIGHTS = (1u << 4),
    HBI_CAP_SUPPORTS_CONTINUOUS_BATCHING = (1u << 5),
    HBI_CAP_SUPPORTS_LONG_CONTEXT = (1u << 6)
} hbi_model_capability;

/* ── Model statistics (adapter-level) ───────────────────────────────────── */

/* Timing and resource counters collected by the adapter during initialization
 * and graph construction. The runtime merges these with load-session statistics
 * for full pipeline observability. */
typedef struct hbi_model_statistics {
    uint64_t init_time_ns;         /* adapter->init wall time */
    uint64_t graph_build_time_ns;  /* adapter->build_graph wall time */
    uint32_t tensors_registered;   /* tensors registered by the adapter */
    uint64_t adapter_memory_bytes; /* adapter-private memory footprint */
    uint32_t graph_nodes;          /* nodes added to the execution graph */
    uint32_t graph_values;         /* values (edges) in the execution graph */
} hbi_model_statistics;

/* ── Model adapter vtable ───────────────────────────────────────────────── */

/* Forward-declare the context as opaque; defined in adapter_internal.h. */
typedef struct hbi_model_context hbi_model_context;

/* The adapter vtable. Each model family provides one static instance of this
 * struct. The runtime interacts ONLY through these function pointers; it never
 * knows the architecture directly. All callbacks may return HBI_ERR_* on
 * failure and set the thread-local error record. */
typedef struct hbi_model_adapter {
    const char *name;                      /* e.g. "llama", "glm", "mock" */
    hbi_adapter_architecture architecture; /* which family this serves */

    /* Initialize adapter-private state from a completed load session.
     * Called once after hbi_model_load() succeeds. The adapter may inspect
     * metadata to configure itself but must NOT load weight data. */
    hbi_status (*init)(const struct hbi_model_adapter *self, const hbi_load_session *session,
                       hbi_allocator *allocator);

    /* Validate that the load-session metadata contains all required keys for
     * this architecture (e.g. "hidden_size", "num_attention_heads"). Returns
     * HBI_ERR_NOT_FOUND for missing keys or HBI_ERR_CORRUPT for bad values. */
    hbi_status (*validate_metadata)(const struct hbi_model_adapter *self,
                                    const hbi_model_metadata *metadata);

    /* Fill a model descriptor from the load-session metadata. The adapter
     * translates architecture-specific key names into the generic descriptor. */
    hbi_status (*build_descriptor)(const struct hbi_model_adapter *self,
                                   const hbi_model_metadata *metadata,
                                   hbi_model_descriptor *out_descriptor);

    /* Populate a graph builder with this model's forward computation graph.
     * The adapter adds inputs, constants, and op-nodes; the runtime will call
     * hbi_graph_build() after this returns to finalize. */
    hbi_status (*build_graph)(const struct hbi_model_adapter *self, hbi_graph_builder *builder,
                              const hbi_model_descriptor *descriptor);

    /* Verify that the manifest contains all tensors this model needs, and that
     * their shapes/dtypes match the descriptor. Called after build_descriptor. */
    hbi_status (*register_tensors)(const struct hbi_model_adapter *self,
                                   const hbi_model_manifest *manifest,
                                   const hbi_model_descriptor *descriptor);

    /* Create a runtime context for inference. The context holds adapter-private
     * state (e.g. per-layer buffers, KV cache config) needed for the forward
     * pass. Must be called after register_tensors succeeds. */
    hbi_status (*create_context)(const struct hbi_model_adapter *self,
                                 const hbi_model_descriptor *descriptor, hbi_allocator *allocator,
                                 hbi_model_context **out_context);

    /* Destroy a context created by create_context. NULL-safe. */
    void (*destroy_context)(const struct hbi_model_adapter *self, hbi_model_context *context);

    /* Report capabilities as a bitmask of hbi_model_capability flags. */
    uint32_t (*get_capabilities)(const struct hbi_model_adapter *self);

    /* Copy adapter-collected statistics into *out. */
    hbi_status (*get_statistics)(const struct hbi_model_adapter *self, hbi_model_statistics *out);

    /* Tear down adapter-private global state (if any). Called at engine
     * shutdown. NULL-safe. */
    void (*shutdown)(const struct hbi_model_adapter *self);
} hbi_model_adapter;

/* ── Adapter registry ───────────────────────────────────────────────────── */

/* Register a model adapter. Called at init time, before workers.
 * Fails HBI_ERR_INVALID_ARG on NULL or missing required fields.
 * Fails HBI_ERR_STATE if the registry is full or a duplicate name exists. */
hbi_status hbi_adapter_register(const hbi_model_adapter *adapter);

/* Find the adapter registered for a given architecture name (exact string
 * match against adapter->name). Returns NULL if no match. */
const hbi_model_adapter *hbi_adapter_find(const char *architecture_name);

/* Find an adapter by architecture enum. Returns the first match or NULL. */
const hbi_model_adapter *hbi_adapter_find_by_arch(hbi_adapter_architecture arch);

/* Number of registered adapters. */
int hbi_adapter_count(void);

/* Clear all registered adapters. For test isolation. */
void hbi_adapter_registry_clear(void);

/* ── Adapter lifecycle helpers ──────────────────────────────────────────── */

/* Resolve the adapter for a loaded model session: reads the "architecture"
 * metadata key, looks up the adapter, and returns it. Fails HBI_ERR_NOT_FOUND
 * if no matching adapter is registered. */
hbi_status hbi_adapter_resolve(const hbi_load_session *session,
                               const hbi_model_adapter **out_adapter);

/* Run the full adapter initialization pipeline on a loaded session:
 *   1. init(adapter, session, allocator)
 *   2. validate_metadata(adapter, metadata)
 *   3. build_descriptor(adapter, metadata, descriptor)
 *   4. register_tensors(adapter, manifest, descriptor)
 * On success, *out_descriptor is filled and *out_stats has timings. */
hbi_status hbi_adapter_init_model(const hbi_model_adapter *adapter, const hbi_load_session *session,
                                  hbi_allocator *allocator, hbi_model_descriptor *out_descriptor,
                                  hbi_model_statistics *out_stats);

/* ── Module identity / self-test ────────────────────────────────────────── */

const char *hbi_adapter_name(void);
hbi_status hbi_adapter_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_ADAPTER_H */
