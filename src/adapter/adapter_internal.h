/* adapter_internal.h — private to the `adapter` module.
 *
 * Concrete definitions for opaque handles declared in adapter.h.
 * Nothing here is visible to other modules. Registry capacity, the model
 * context concrete struct, and mock-adapter registration helper live here.
 */
#ifndef HB_ADAPTER_INTERNAL_H
#define HB_ADAPTER_INTERNAL_H

#include "adapter/adapter.h"

/* ── Adapter registry capacity ─────────────────────────────────────────── */

/* Maximum simultaneously registered adapters. Ample for the bootstrap
 * (GPT-OSS, GLM, DeepSeek, Qwen, Llama, Mistral, Gemma, Falcon = 8 + mock). */
#define HBI_ADAPTER_REGISTRY_MAX 16u

/* ── Model context (concrete definition) ─────────────────────────────────
 * The opaque hbi_model_context from adapter.h is this struct. Adapters may
 * cast `private_data` to their own internal state type. The framework owns
 * the outer allocation; the adapter owns whatever private_data points to. */
struct hbi_model_context {
    hbi_allocator *allocator;         /* borrowed allocator for teardown */
    const hbi_model_adapter *adapter; /* back-pointer to owning adapter */
    hbi_model_descriptor descriptor;  /* snapshot of the descriptor used */
    hbi_model_statistics stats;       /* accumulated runtime statistics */
    void *private_data;               /* adapter-private state blob */
    size_t private_data_size;         /* for accounting / diagnostics */
    bool initialized;                 /* lifecycle guard */
};

/* ── Mock adapter registration ─────────────────────────────────────────── */

/* Register the built-in mock adapter used for testing. Defined in
 * adapter_mock.c. Called by tests; not part of the public API. */
hbi_status hbi_adapter_mock_register(void);

/* Get the static mock adapter instance (for direct use in tests). */
const hbi_model_adapter *hbi_adapter_mock_get(void);

#endif /* HB_ADAPTER_INTERNAL_H */
