/* model_internal.h — private to the `model` module.
 *
 * Concrete definitions for opaque handles declared in model.h.
 * Nothing here is visible to other modules.
 */
#ifndef HB_MODEL_INTERNAL_H
#define HB_MODEL_INTERNAL_H

#include "model/model.h"

/* ── Manifest ────────────────────────────────────────────────────────────── */

#define HBI_MANIFEST_INITIAL_CAP 64u

struct hbi_model_manifest {
    hbi_allocator *allocator;
    hbi_tensor_entry *entries;
    uint32_t count;
    uint32_t capacity;
};

/* ── Metadata ────────────────────────────────────────────────────────────── */

#define HBI_METADATA_INITIAL_CAP 32u

typedef struct hbi_metadata_pair {
    char key[HBI_METADATA_KEY_MAX];
    char value[HBI_METADATA_VALUE_MAX];
} hbi_metadata_pair;

struct hbi_model_metadata {
    hbi_allocator *allocator;
    hbi_metadata_pair *pairs;
    uint32_t count;
    uint32_t capacity;
};

/* ── Load session ────────────────────────────────────────────────────────── */

typedef enum hbi_load_state {
    HBI_LOAD_STATE_INIT = 0,
    HBI_LOAD_STATE_PARSED,
    HBI_LOAD_STATE_VALIDATED,
    HBI_LOAD_STATE_COMPLETE,
    HBI_LOAD_STATE_FAILED
} hbi_load_state;

struct hbi_load_session {
    hbi_allocator *allocator;
    hbi_model_manifest *manifest;
    hbi_model_metadata *metadata;
    hbi_load_statistics stats;
    hbi_load_options options;
    hbi_model_format detected_format;
    const hbi_format_handler *handler;
    hbi_load_state state;
};

/* ── Format handler registry ─────────────────────────────────────────────── */

#define HBI_FORMAT_HANDLER_MAX 8

/* Init functions called by model.c to register built-in handlers. */
hbi_status hbi_format_gguf_register(void);
hbi_status hbi_format_safetensors_register(void);

#endif /* HB_MODEL_INTERNAL_H */
