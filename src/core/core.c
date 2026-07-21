/* core.c — Runtime Context lifecycle and subsystem ownership (DD-021).
 *
 * Bring-up order (dependency-respecting):
 *   1. adopt allocator (system allocator if none supplied)
 *   2. allocate the context from that allocator
 *   3. apply the logging level
 *   4. query the host device report (cached)
 *   5. optionally enable the profiler
 *   6. build the effective foundation config
 *   7. start the thread pool (sized from the device if not specified)
 * Teardown runs in strict reverse, and a failed create unwinds exactly the
 * steps it completed. The context never calls exit() (DD-011).
 */
#include "core/core_internal.h"

#include "profiler/profiler.h"

#include <string.h>

/* ── Foundation config schema ────────────────────────────────────────────────
 * The minimal, typed config the context exposes via hbi_core_config_get(). It is
 * intentionally small; higher layers extend configuration through their own
 * schemas. Keys are dotted for namespacing. */
static const hbi_config_desc k_core_schema[] = {
    {"core.log_level", HBI_CFG_INT, "HB_LOG_LEVEL", "logging verbosity (0=trace .. 5=profiling)",
     false, HBI_LOG_INFO, 0, NULL, 0, HBI_LOG_LEVEL_COUNT - 1, 0, 0},
    {"core.workers", HBI_CFG_INT, "HB_WORKERS", "thread-pool worker count (0 => device default)",
     false, 0, 0, NULL, 0, 4096, 0, 0},
    {"core.queue_capacity", HBI_CFG_UINT, "HB_QUEUE_CAPACITY", "thread-pool queue depth", false, 0,
     256, NULL, 0, 0, 1, 1u << 20},
    {"core.profiling", HBI_CFG_BOOL, "HB_PROFILING", "enable the profiler at startup", false, 0, 0,
     NULL, 0, 0, 0, 0},
    {0} /* terminator */
};

const char *hbi_core_name(void) {
    return "core";
}

const char *hbi_core_state_str(hbi_core_state state) {
    switch (state) {
    case HBI_CORE_UNINIT:
        return "uninit";
    case HBI_CORE_READY:
        return "ready";
    case HBI_CORE_DEAD:
        return "dead";
    }
    return "unknown";
}

hbi_status hbi_core_config_default(hbi_core_config *cfg) {
    if (cfg == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    cfg->log_level = HBI_LOG_INFO;
    cfg->num_workers = 0; /* => device logical cores */
    cfg->queue_capacity = 256;
    cfg->enable_profiling = false;
    cfg->allocator = NULL; /* => system allocator */
    return HBI_OK;
}

hbi_status hbi_core_create(hbi_core **out, const hbi_core_config *cfg_in) {
    if (out == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    *out = NULL;

    hbi_core_config cfg;
    if (cfg_in != NULL) {
        cfg = *cfg_in;
    } else {
        (void)hbi_core_config_default(&cfg);
    }

    /* 1. Allocator. */
    hbi_allocator *alloc = cfg.allocator ? cfg.allocator : hbi_allocator_system();

    /* 2. Context object. */
    hbi_core *core = (hbi_core *)hbi_alloc(alloc, sizeof(*core), 0, HBI_MEM_GENERAL);
    if (core == NULL) {
        return HBI_ERR_OOM; /* error record already set by the allocator */
    }
    memset(core, 0, sizeof(*core));
    core->allocator = alloc;
    core->state = HBI_CORE_UNINIT;

    /* 3. Logging level. */
    hbi_log_set_level(cfg.log_level);

    /* 4. Device report (never fails for a non-NULL out). */
    (void)hbi_device_query(&core->device);

    /* 5. Profiler (optional). */
    if (cfg.enable_profiling) {
        hbi_prof_set_enabled(true);
        core->profiling_on = true;
    }

    /* 6. Effective config. */
    hbi_status st = hbi_config_create(&core->config, k_core_schema);
    if (st != HBI_OK) {
        goto fail_after_profiler;
    }
    (void)hbi_config_set_int(core->config, "core.log_level", (int64_t)cfg.log_level);
    (void)hbi_config_set_bool(core->config, "core.profiling", cfg.enable_profiling);

    /* 7. Thread pool, sized from the device when unspecified. */
    int workers = cfg.num_workers > 0 ? cfg.num_workers : core->device.logical_cores;
    if (workers < 1) {
        workers = 1;
    }
    size_t qcap = cfg.queue_capacity ? cfg.queue_capacity : 256;
    st = hbi_threadpool_create(&core->pool, workers, qcap);
    if (st != HBI_OK) {
        goto fail_after_config;
    }
    (void)hbi_config_set_int(core->config, "core.workers", (int64_t)workers);
    (void)hbi_config_set_uint(core->config, "core.queue_capacity", (uint64_t)qcap);

    core->state = HBI_CORE_READY;
    *out = core;
    return HBI_OK;

    /* Unwind paths — release exactly what was built, in reverse. */
fail_after_config:
    hbi_config_destroy(core->config);
fail_after_profiler:
    if (core->profiling_on) {
        hbi_prof_set_enabled(false);
    }
    hbi_free(alloc, core);
    return st;
}

void hbi_core_destroy(hbi_core *core) {
    if (core == NULL) {
        return;
    }
    /* Mark dead first so any concurrent accessor misuse fails cleanly. */
    core->state = HBI_CORE_DEAD;

    /* Finalize registered subsystems in reverse registration order. */
    for (size_t i = core->subsystem_count; i-- > 0;) {
        hbi_core_entry *e = &core->subsystems[i];
        if (e->fini != NULL && e->ptr != NULL) {
            e->fini(e->ptr);
        }
    }
    core->subsystem_count = 0;

    /* Reverse of bring-up: pool → config → profiler. */
    hbi_threadpool_destroy(core->pool);
    core->pool = NULL;

    hbi_config_destroy(core->config);
    core->config = NULL;

    if (core->profiling_on) {
        hbi_prof_set_enabled(false);
        core->profiling_on = false;
    }

    /* Free the context from the allocator it was created with. */
    hbi_allocator *alloc = core->allocator;
    hbi_free(alloc, core);
}

hbi_core_state hbi_core_get_state(const hbi_core *core) {
    return core ? core->state : HBI_CORE_UNINIT;
}

/* ── Accessors ─────────────────────────────────────────────────────────────── */

static bool core_ready(const hbi_core *core) {
    return core != NULL && core->state == HBI_CORE_READY;
}

hbi_allocator *hbi_core_allocator(const hbi_core *core) {
    return core_ready(core) ? core->allocator : NULL;
}

hbi_threadpool *hbi_core_threadpool(const hbi_core *core) {
    return core_ready(core) ? core->pool : NULL;
}

const hbi_config *hbi_core_config_get(const hbi_core *core) {
    return core_ready(core) ? core->config : NULL;
}

hbi_status hbi_core_device(const hbi_core *core, hbi_device_info *out) {
    if (out == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    if (!core_ready(core)) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "hbi_core_device: context not READY");
    }
    *out = core->device;
    return HBI_OK;
}

/* ── Subsystem registry ──────────────────────────────────────────────────────── */

hbi_status hbi_core_register(hbi_core *core, const char *name, void *ptr,
                             hbi_core_subsystem_fini fini) {
    if (core == NULL || name == NULL || ptr == NULL) {
        return HBI_ERR_INVALID_ARG;
    }
    if (core->state != HBI_CORE_READY) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "hbi_core_register: context not READY");
    }
    for (size_t i = 0; i < core->subsystem_count; ++i) {
        if (strcmp(core->subsystems[i].name, name) == 0) {
            return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "hbi_core_register: duplicate subsystem '%s'",
                                name);
        }
    }
    if (core->subsystem_count >= HBI_CORE_MAX_SUBSYSTEMS) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "hbi_core_register: subsystem table full");
    }
    hbi_core_entry *e = &core->subsystems[core->subsystem_count++];
    e->name = name;
    e->ptr = ptr;
    e->fini = fini;
    return HBI_OK;
}

void *hbi_core_lookup(const hbi_core *core, const char *name) {
    if (!core_ready(core) || name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < core->subsystem_count; ++i) {
        if (strcmp(core->subsystems[i].name, name) == 0) {
            return core->subsystems[i].ptr;
        }
    }
    return NULL;
}

size_t hbi_core_subsystem_count(const hbi_core *core) {
    return core ? core->subsystem_count : 0;
}

/* ── Self-test ─────────────────────────────────────────────────────────────────
 * Exercise a full lifecycle so a bare `ctest` on this module proves bring-up and
 * teardown work with defaults. */
hbi_status hbi_core_selftest(void) {
    hbi_core *core = NULL;
    hbi_status st = hbi_core_create(&core, NULL);
    if (st != HBI_OK || core == NULL) {
        return st == HBI_OK ? HBI_ERR_INTERNAL : st;
    }
    if (hbi_core_get_state(core) != HBI_CORE_READY || hbi_core_allocator(core) == NULL ||
        hbi_core_threadpool(core) == NULL || hbi_core_config_get(core) == NULL) {
        hbi_core_destroy(core);
        return HBI_ERR_INTERNAL;
    }
    hbi_core_destroy(core);
    return HBI_OK;
}
