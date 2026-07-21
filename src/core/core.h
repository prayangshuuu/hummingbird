/* core.h — the Runtime Context: lifecycle, foundation-subsystem ownership, and
 * subsystem registration for an embeddable engine instance (PROJECT_CONTEXT §3.2
 * lifecycle, DD-021).
 *
 * Core-public header for the `core` module. Symbols are prefixed `hbi_`
 * (internal, no stability guarantee); external embedders use the public ABI in
 * <hummingbird/hummingbird.h>, which is a thin layer over this.
 *
 * What the Runtime Context is (and is NOT):
 *   - It is the single owner of the FOUNDATION subsystems every future part of
 *     the engine needs: the active allocator, the logger's configuration, the
 *     generic thread pool, the effective configuration, and the host device
 *     report. It brings them up in dependency order and tears them down in
 *     reverse, exactly once, with an explicit state machine.
 *   - It is NOT the forward-pass orchestrator. Running a model, sequencing
 *     layers, and producing logits belong to the `runtime` module (layer 10),
 *     which will sit ABOVE this and borrow the context. Keeping lifecycle
 *     (low, foundational) separate from orchestration (high) is why this module
 *     exists as its own layer (DD-021).
 *
 * There are NO hidden globals: everything the engine needs hangs off an explicit
 * hbi_core object the caller holds. This is the deliberate departure from
 * Colibrì's process-wide `Model` god-struct (PROJECT_CONTEXT §5).
 *
 * Thread-safety: create/destroy are single-threaded lifecycle operations. Once a
 * context is READY, its accessors return objects that are themselves thread-safe
 * to use (the system allocator, the pool). Do not create/destroy concurrently
 * with use.
 */
#ifndef HB_CORE_H
#define HB_CORE_H

#include "common/common.h"
#include "config/config.h"
#include "device/device.h"
#include "logging/logging.h"
#include "memory/memory.h"
#include "threadpool/threadpool.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Lifecycle state ─────────────────────────────────────────────────────────
 * An explicit, inspectable state machine. Calls that require a particular state
 * return HBI_ERR_STATE otherwise, rather than crashing. */
typedef enum hbi_core_state {
    HBI_CORE_UNINIT = 0, /* not yet created / already destroyed */
    HBI_CORE_READY = 1,  /* fully initialized, usable */
    HBI_CORE_DEAD = 2    /* shutdown begun; no longer usable */
} hbi_core_state;

const char *hbi_core_state_str(hbi_core_state state);

/* Opaque Runtime Context handle. The caller holds one; there are no globals. */
typedef struct hbi_core hbi_core;

/* ── Configuration for bring-up ──────────────────────────────────────────────
 * Everything needed to initialize a context, with safe defaults via
 * hbi_core_config_default(). All fields are optional: a zeroed struct passed
 * through the defaulter yields a working single-threaded context. */
typedef struct hbi_core_config {
    hbi_log_level log_level;  /* initial logging level */
    int num_workers;          /* thread-pool workers; <=0 => device default */
    size_t queue_capacity;    /* thread-pool queue depth; 0 => a sane default */
    bool enable_profiling;    /* turn the profiler on at startup */
    hbi_allocator *allocator; /* allocator to adopt; NULL => system allocator */
} hbi_core_config;

/* Fill *cfg with defaults (INFO logging, workers = device logical cores, a
 * modest queue, profiling off, system allocator). Returns HBI_ERR_INVALID_ARG
 * if cfg is NULL. */
hbi_status hbi_core_config_default(hbi_core_config *cfg);

/* ── Lifecycle ───────────────────────────────────────────────────────────────
 * Create brings subsystems up in dependency order: adopt allocator → apply log
 * level → query device → (optional) enable profiler → start thread pool. On any
 * failure it unwinds whatever it already started and returns without leaking, so
 * a failed create leaves *out == NULL. Passing cfg == NULL uses all defaults. */
hbi_status hbi_core_create(hbi_core **out, const hbi_core_config *cfg);

/* Tear subsystems down in reverse order (stop pool → disable profiler → ...),
 * exactly once, then free the context. NULL is a no-op. Idempotent per handle in
 * the sense that the handle is invalid afterward; do not call twice on the same
 * pointer. */
void hbi_core_destroy(hbi_core *core);

/* Current lifecycle state. NULL yields HBI_CORE_UNINIT. */
hbi_core_state hbi_core_get_state(const hbi_core *core);

/* ── Accessors ───────────────────────────────────────────────────────────────
 * Borrowed pointers into the context's subsystems, valid until destroy. They
 * return NULL if core is NULL or not READY (so misuse is visible, not fatal). */
hbi_allocator *hbi_core_allocator(const hbi_core *core);
hbi_threadpool *hbi_core_threadpool(const hbi_core *core);
const hbi_config *hbi_core_config_get(const hbi_core *core);

/* Copy the host device report into *out. Returns HBI_ERR_STATE if not READY,
 * HBI_ERR_INVALID_ARG on NULL args. */
hbi_status hbi_core_device(const hbi_core *core, hbi_device_info *out);

/* ── Subsystem registry ──────────────────────────────────────────────────────
 * A generic, name-keyed table so higher layers can attach their own subsystems
 * (a model manager, a backend registry, ...) to a context without core needing
 * to know their types. Each entry is an opaque pointer plus an optional
 * finalizer run (in reverse registration order) at destroy. This is the
 * extension point that keeps `core` model- and backend-agnostic (DD-021).
 *
 * Names are borrowed (must outlive the context; static literals recommended) and
 * must be unique. Register during single-threaded setup. */
typedef void (*hbi_core_subsystem_fini)(void *ptr);

/* Attach `ptr` under `name` with an optional finalizer. Returns
 * HBI_ERR_INVALID_ARG (NULL core/name/ptr), HBI_ERR_STATE (not READY),
 * HBI_ERR_CORRUPT (duplicate name), or HBI_ERR_OOM (table full). */
hbi_status hbi_core_register(hbi_core *core, const char *name, void *ptr,
                             hbi_core_subsystem_fini fini);

/* Look up a registered subsystem by name. Returns NULL if absent or core not
 * READY. Does not transfer ownership. */
void *hbi_core_lookup(const hbi_core *core, const char *name);

/* Number of registered subsystems. */
size_t hbi_core_subsystem_count(const hbi_core *core);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_core_name(void);
hbi_status hbi_core_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_CORE_H */
