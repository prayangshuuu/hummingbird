/* session.c — Runtime session lifecycle (RFC-019).
 *
 * Implements:
 *   hbi_runtime_session_create()
 *   hbi_runtime_session_destroy()
 *   hbi_session_cancel()
 *   hbi_session_reset()
 *   hbi_session_pipeline_reset()  [internal helper]
 */
#include "runtime/session.h"
#include "model/model_internal.h"
#include "runtime/runtime.h"
#include "runtime/runtime_internal.h"
#include "runtime/sampler.h"
#include "stream/stream.h"

#include <stdatomic.h>
#include <string.h>

/* ── Default config ───────────────────────────────────────────────────── */

static void apply_default_config(hbi_runtime_config *cfg) {
    if (cfg->max_new_tokens == 0) {
        cfg->max_new_tokens = 128u;
    }
    /* greedy shorthand overrides the sampler field */
    if (cfg->greedy) {
        cfg->sampler = HBI_SAMPLER_GREEDY;
    }
}

/* ── Internal pipeline-state teardown ───────────────────────────────────
 * Releases the objects that are rebuilt on each hbi_runtime_generate() call.
 * Does NOT touch the long-lived objects (kv_manager, kv_ctx, model_ctx). */
void hbi_session_pipeline_reset(hbi_runtime_session *s) {
    if (!s)
        return;

    if (s->plan_memory) {
        hbi_memory_plan_destroy(s->plan_memory);
        s->plan_memory = NULL;
    }
    if (s->plan) {
        if (s->scheduler) {
            hbi_execution_plan_destroy(s->scheduler, s->plan);
        }
        s->plan = NULL;
    }
    if (s->scheduler) {
        hbi_scheduler_destroy(s->scheduler);
        s->scheduler = NULL;
    }
    if (s->graph) {
        hbi_graph_destroy(s->graph);
        s->graph = NULL;
    }
    if (s->backend_mgr) {
        hbi_backend_manager_destroy(s->backend_mgr);
        s->backend_mgr = NULL;
    }
}

/* ── hbi_runtime_session_create ─────────────────────────────────────────
 *
 * Ownership contract:
 *   model, adapter, tok_mgr  — BORROWED; caller guarantees they outlive s.
 *   allocator                — BORROWED for the session's lifetime.
 *   s->kv_manager            — OWNED; created here, destroyed in destroy().
 *   s->model_ctx             — OWNED; created here if adapter != NULL.
 */
hbi_status hbi_runtime_session_create(const hbi_load_session *model,
                                      const hbi_model_adapter *adapter,
                                      const hbi_tokenizer_manager *tok_mgr,
                                      const hbi_runtime_config *config, hbi_allocator *allocator,
                                      hbi_runtime_session **out) {
    if (!allocator) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "runtime_session_create: allocator is NULL");
    }
    if (!out) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "runtime_session_create: out is NULL");
    }

    hbi_runtime_session *s = (hbi_runtime_session *)hbi_alloc(
        allocator, sizeof(hbi_runtime_session), 8, HBI_MEM_GENERAL);
    if (!s) {
        return HBI_ERR_SET(HBI_ERR_OOM, 0, "runtime_session_create: session alloc failed");
    }
    memset(s, 0, sizeof(*s));

    /* Store borrowed references. */
    s->model = model;
    s->adapter = adapter;
    s->tok_mgr = tok_mgr;
    s->allocator = allocator;

    /* Copy (or default) configuration. */
    if (config) {
        s->config = *config;
    } else {
        memset(&s->config, 0, sizeof(s->config));
        s->config.greedy = true;
    }
    apply_default_config(&s->config);

    /* Initialise the atomic cancel flag to false. */
    atomic_store_explicit(&s->cancel_flag, false, memory_order_relaxed);

    /* Create the KV cache manager backed by the session allocator.
     * NULL kv_alloc_override → use the default contiguous allocator. */
    hbi_status st = hbi_kv_manager_create(allocator, NULL, &s->kv_manager);
    if (st != HBI_OK) {
        hbi_free(allocator, s);
        return st; /* error already set by kv module */
    }

    /* If the caller provided an adapter, run the full adapter init pipeline
     * to populate s->descriptor and create s->model_ctx.
     * This is optional: test code may pass NULL adapter. */
    if (adapter && model) {
        hbi_model_statistics stats;
        memset(&stats, 0, sizeof(stats));
        st = hbi_adapter_init_model(adapter, model, allocator, &s->descriptor, &stats);
        if (st != HBI_OK) {
            hbi_kv_manager_destroy(s->kv_manager);
            hbi_free(allocator, s);
            return st;
        }

        st = adapter->create_context(adapter, &s->descriptor, allocator, &s->model_ctx);
        if (st != HBI_OK) {
            hbi_kv_manager_destroy(s->kv_manager);
            hbi_free(allocator, s);
            return st;
        }
    }

    /* Instantiate Stream Engine and register blocks from the manifest */
    if (model) {
        st = hbi_stream_engine_create(4ULL * 1024 * 1024 * 1024, 0, HB_CACHE_POLICY_LRU, NULL,
                                      &s->stream_engine);
        if (st != HBI_OK) {
            hbi_runtime_session_destroy(s);
            return st;
        }

        const hbi_model_manifest *manifest = hbi_load_session_manifest(model);
        if (manifest) {
            for (uint32_t i = 0; i < hbi_model_manifest_count(manifest); i++) {
                const hbi_tensor_entry *entry = hbi_model_manifest_entry(manifest, i);

                hbi_stream_block_desc desc;
                memset(&desc, 0, sizeof(desc));
                desc.tensor_id = i;
                /* Note: file_path access requires model_internal.h, assuming it is included or
                 * available */
                desc.file_path = model->options.model_path;
                desc.handler = model->handler;
                desc.entry = entry;
                desc.dtype = entry->dtype;
                desc.shape = entry->shape;

                if (entry->residency == HBI_RESIDENCY_RESIDENT) {
                    desc.initial_status = HB_RESIDENCY_RESIDENT;
                    desc.initial_tier = HB_TIER_RAM;
                } else {
                    desc.initial_status = HB_RESIDENCY_EVICTED;
                    desc.initial_tier = HB_TIER_DISK;
                }

                hbi_stream_block *out_block = NULL;
                st = hbi_stream_register_block(s->stream_engine, &desc, &out_block);
                if (st != HBI_OK) {
                    hbi_runtime_session_destroy(s);
                    return st;
                }
            }
        }
    }

    *out = s;
    return HBI_OK;
}

/* ── hbi_runtime_session_destroy ────────────────────────────────────────── */

void hbi_runtime_session_destroy(hbi_runtime_session *s) {
    if (!s)
        return;

    /* Tear down per-generate pipeline state first. */
    hbi_session_pipeline_reset(s);

    /* Destroy adapter model context. */
    if (s->model_ctx && s->adapter && s->adapter->destroy_context) {
        s->adapter->destroy_context(s->adapter, s->model_ctx);
        s->model_ctx = NULL;
    }

    /* Destroy KV context, then manager. */
    if (s->kv_ctx && s->kv_manager) {
        hbi_kv_context_destroy(s->kv_manager, s->kv_ctx);
        s->kv_ctx = NULL;
    }
    if (s->kv_manager) {
        hbi_kv_manager_destroy(s->kv_manager);
        s->kv_manager = NULL;
    }

    hbi_free(s->allocator, s);
}

/* ── hbi_session_cancel / hbi_session_reset ─────────────────────────────── */

hbi_status hbi_session_cancel(hbi_runtime_session *s) {
    if (!s) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "session_cancel: NULL session");
    }
    atomic_store_explicit(&s->cancel_flag, true, memory_order_release);
    return HBI_OK;
}

hbi_status hbi_session_reset(hbi_runtime_session *s) {
    if (!s) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "session_reset: NULL session");
    }
    atomic_store_explicit(&s->cancel_flag, false, memory_order_release);
    return HBI_OK;
}
