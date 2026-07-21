/* core_internal.h — private to the `core` module.
 *
 * The concrete hbi_core definition and helpers. Not visible to other modules;
 * they see only the opaque handle in core.h.
 */
#ifndef HB_CORE_INTERNAL_H
#define HB_CORE_INTERNAL_H

#include "core/core.h"

/* A registered subsystem: an opaque pointer under a borrowed name, with an
 * optional finalizer run at destroy in reverse registration order. */
enum { HBI_CORE_MAX_SUBSYSTEMS = 32 };

typedef struct hbi_core_entry {
    const char *name;             /* borrowed; must outlive the context */
    void *ptr;                    /* borrowed; owned by the registrant */
    hbi_core_subsystem_fini fini; /* optional finalizer, may be NULL */
} hbi_core_entry;

struct hbi_core {
    hbi_core_state state;

    /* Foundation subsystems the context owns/coordinates. */
    hbi_allocator *allocator; /* adopted (system allocator by default) */
    hbi_threadpool *pool;     /* owned: created at bring-up, stopped at teardown */
    hbi_config *config;       /* owned: the effective foundation config */
    hbi_device_info device;   /* cached host report */
    bool profiling_on;        /* whether we enabled the profiler */

    /* Subsystem registry (fixed capacity; no allocation on the hot path). */
    hbi_core_entry subsystems[HBI_CORE_MAX_SUBSYSTEMS];
    size_t subsystem_count;
};

#endif /* HB_CORE_INTERNAL_H */
