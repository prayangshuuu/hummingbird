/* model.h — Model Loader Framework: format-independent loading pipeline,
 * tensor manifest, metadata validation, and streaming integration (RFC-011).
 *
 * Core-public header for the `model` module (layer 7). Other modules include
 * this; external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 *
 * ── Design (RFC-011) ─────────────────────────────────────────────────────────
 * The Model Loader discovers, validates, and *indexes* model assets without
 * eagerly loading weight data. The output is a manifest — a table of tensor
 * descriptors with file offsets and sizes. Actual weight bytes are loaded on
 * demand by the Streaming Engine (or eagerly if HBI_LOAD_EAGER is requested).
 *
 * File formats (GGUF, Safetensors, future ONNX) are abstracted behind a
 * pluggable format-handler vtable. The core pipeline is format-independent:
 *   1. Validate options
 *   2. Detect format (iterate registered handlers)
 *   3. Parse metadata via the handler (builds the manifest)
 *   4. Validate the manifest (duplicates, shapes, dtypes)
 *   5. Record statistics
 *   6. Return a session handle
 *
 * ── Ownership ────────────────────────────────────────────────────────────────
 * The load session owns the manifest and metadata. Higher layers (executor,
 * streaming engine) borrow entries. The session must outlive any streaming
 * operation on its tensors.
 *
 * ── Thread-safety ────────────────────────────────────────────────────────────
 * The format-handler registry is populated at init time (before workers).
 * A load session is not thread-safe; serialize externally or use one per thread.
 * The error record is thread-local, as everywhere else.
 */
#ifndef HB_MODEL_H
#define HB_MODEL_H

#include "common/common.h"
#include "memory/memory.h"
#include "tensor/tensor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Model format enum ───────────────────────────────────────────────────── */

typedef enum hbi_model_format {
    HBI_MODEL_FORMAT_UNKNOWN = 0,
    HBI_MODEL_FORMAT_GGUF,
    HBI_MODEL_FORMAT_SAFETENSORS,
    HBI_MODEL_FORMAT_COUNT /* sentinel */
} hbi_model_format;

/* Stable lower-case spelling. Never NULL. */
const char *hbi_model_format_str(hbi_model_format fmt);

/* ── Tensor entry (one row in the manifest) ──────────────────────────────── */

#define HBI_TENSOR_NAME_MAX 256u

/* Residency hint: where the loader suggests this tensor should initially live.
 * The streaming engine makes the final placement decision. */
typedef enum hbi_residency_hint {
    HBI_RESIDENCY_DEFAULT = 0, /* let the engine decide */
    HBI_RESIDENCY_RESIDENT,    /* keep in RAM at all times (e.g. embeddings) */
    HBI_RESIDENCY_STREAMABLE,  /* can be evicted / streamed from disk */
    HBI_RESIDENCY_COLD         /* rarely accessed; prefer disk */
} hbi_residency_hint;

typedef struct hbi_tensor_entry {
    char name[HBI_TENSOR_NAME_MAX]; /* fully-qualified tensor name */
    hbi_shape shape;                /* declared shape */
    hbi_dtype dtype;                /* element type */
    uint64_t file_offset;           /* byte offset into the model file/shard */
    uint64_t byte_size;             /* total bytes on disk for this tensor */
    size_t required_alignment;      /* minimum alignment for the data buffer */
    uint32_t shard_index;           /* index of the shard file (0 for single-file) */
    hbi_residency_hint residency;   /* placement suggestion */
} hbi_tensor_entry;

/* ── Model manifest (opaque) ─────────────────────────────────────────────── */

typedef struct hbi_model_manifest hbi_model_manifest;

/* Create an empty manifest. */
hbi_status hbi_model_manifest_create(hbi_allocator *allocator, hbi_model_manifest **out);

/* Destroy a manifest and free all entries. NULL-safe. */
void hbi_model_manifest_destroy(hbi_model_manifest *manifest);

/* Add a tensor entry to the manifest. The entry is copied.
 * Fails HBI_ERR_STATE if a tensor with the same name already exists.
 * Fails HBI_ERR_INVALID_ARG on NULL or invalid entry fields. */
hbi_status hbi_model_manifest_add(hbi_model_manifest *manifest, const hbi_tensor_entry *entry);

/* Number of registered tensor entries. */
uint32_t hbi_model_manifest_count(const hbi_model_manifest *manifest);

/* Access a tensor entry by index. NULL if out of range. */
const hbi_tensor_entry *hbi_model_manifest_entry(const hbi_model_manifest *manifest,
                                                 uint32_t index);

/* Find a tensor entry by name. NULL if not found. */
const hbi_tensor_entry *hbi_model_manifest_find(const hbi_model_manifest *manifest,
                                                const char *name);

/* Validate all entries in the manifest: no duplicates (already enforced on add),
 * valid shapes, valid dtypes, non-zero sizes. Returns the first error found. */
hbi_status hbi_model_manifest_validate(const hbi_model_manifest *manifest);

/* ── Model metadata (key-value pairs from the model file) ────────────────── */

#define HBI_METADATA_KEY_MAX 128u
#define HBI_METADATA_VALUE_MAX 512u

typedef struct hbi_model_metadata hbi_model_metadata;

/* Create empty metadata. */
hbi_status hbi_model_metadata_create(hbi_allocator *allocator, hbi_model_metadata **out);

/* Destroy metadata. NULL-safe. */
void hbi_model_metadata_destroy(hbi_model_metadata *metadata);

/* Set a key-value pair. Overwrites if key already exists.
 * Both key and value are copied. */
hbi_status hbi_model_metadata_set(hbi_model_metadata *metadata, const char *key, const char *value);

/* Get a value by key. Returns NULL if not found. */
const char *hbi_model_metadata_get(const hbi_model_metadata *metadata, const char *key);

/* Number of stored key-value pairs. */
uint32_t hbi_model_metadata_count(const hbi_model_metadata *metadata);

/* ── Load options ────────────────────────────────────────────────────────── */

typedef enum hbi_load_flags {
    HBI_LOAD_DEFAULT = 0u,
    HBI_LOAD_EAGER = (1u << 0), /* load all tensor data immediately */
    HBI_LOAD_STRICT = (1u << 1) /* reject any validation warnings as errors */
} hbi_load_flags;

typedef struct hbi_load_options {
    const char *model_path;       /* path to model file or directory */
    hbi_model_format format_hint; /* UNKNOWN = auto-detect */
    uint32_t flags;               /* bitwise OR of hbi_load_flags */
} hbi_load_options;

/* ── Load statistics ─────────────────────────────────────────────────────── */

typedef struct hbi_load_statistics {
    uint64_t metadata_parse_time_ns;
    uint64_t validation_time_ns;
    uint64_t total_load_time_ns;
    uint32_t tensors_registered;
    uint64_t bytes_indexed;
    uint32_t validation_warnings;
    uint32_t validation_errors;
} hbi_load_statistics;

/* ── Format handler vtable ───────────────────────────────────────────────── */

typedef struct hbi_format_handler {
    const char *name;        /* e.g. "gguf", "safetensors" */
    hbi_model_format format; /* which format this handler owns */

    /* Return true if the file at `path` matches this format (magic byte check).
     * Must not read more than a small header (e.g. 16 bytes). */
    bool (*detect)(const char *path);

    /* Parse the metadata and tensor index from the file. Populate the manifest
     * and metadata without loading tensor data. Returns HBI_ERR_UNSUPPORTED if
     * the handler is a skeleton that cannot yet parse. */
    hbi_status (*parse_metadata)(const char *path, hbi_allocator *allocator,
                                 hbi_model_manifest *manifest, hbi_model_metadata *metadata);

    /* Read tensor data for a single entry into a pre-allocated buffer.
     * `buf` has at least `entry->byte_size` bytes at `entry->required_alignment`.
     * Returns HBI_ERR_UNSUPPORTED if the handler is a skeleton. */
    hbi_status (*read_tensor_data)(const char *path, const hbi_tensor_entry *entry, void *buf,
                                   size_t buf_size);
} hbi_format_handler;

/* Register a format handler. Called at init time, before workers.
 * Fails HBI_ERR_INVALID_ARG on NULL or missing fields.
 * Fails HBI_ERR_STATE if the registry is full. */
hbi_status hbi_format_handler_register(const hbi_format_handler *handler);

/* Auto-detect the format of the file at `path` by querying registered handlers.
 * Returns HBI_MODEL_FORMAT_UNKNOWN if no handler matches. */
hbi_model_format hbi_format_handler_detect(const char *path);

/* Find the registered handler for a given format. NULL if none. */
const hbi_format_handler *hbi_format_handler_find(hbi_model_format fmt);

/* Number of registered format handlers. */
int hbi_format_handler_count(void);

/* Clear all registered format handlers. For test isolation. */
void hbi_format_handler_registry_clear(void);

/* ── Load session ────────────────────────────────────────────────────────── */

typedef struct hbi_load_session hbi_load_session;

/* Run the full model loading pipeline:
 *   1. Validate options
 *   2. Detect format (or use hint)
 *   3. Parse metadata via format handler
 *   4. Validate manifest
 *   5. Record statistics
 * On success, *out_session is a valid session owning the manifest + metadata.
 * On failure, *out_session is NULL and the error record describes the problem. */
hbi_status hbi_model_load(const hbi_load_options *options, hbi_allocator *allocator,
                          hbi_load_session **out_session);

/* Destroy a load session and all owned resources. NULL-safe. */
void hbi_model_load_session_destroy(hbi_load_session *session);

/* Accessors into a completed load session. */
const hbi_model_manifest *hbi_load_session_manifest(const hbi_load_session *session);
const hbi_model_metadata *hbi_load_session_metadata(const hbi_load_session *session);
const hbi_load_statistics *hbi_load_session_statistics(const hbi_load_session *session);
hbi_model_format hbi_load_session_format(const hbi_load_session *session);

/* ── Module identity / self-test ─────────────────────────────────────────── */

const char *hbi_model_name(void);
hbi_status hbi_model_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_MODEL_H */
