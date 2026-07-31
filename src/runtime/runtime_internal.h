/* runtime_internal.h — private to the `runtime` module.
 *
 * Nothing here is visible to other modules. Implementation details,
 * internal structs, and static-helper prototypes live here as the module grows.
 */
#ifndef HB_RUNTIME_INTERNAL_H
#define HB_RUNTIME_INTERNAL_H

#include "adapter/adapter.h"
#include "backend/backend.h"
#include "graph/graph.h"
#include "kv/kv.h"
#include "model/model.h"
#include "planner/planner.h"
#include "runtime/runtime.h"
#include "scheduler/scheduler.h"
#include "tokenizer/tokenizer.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hbi_runtime_session {
    /* Borrowed references */
    const hbi_load_session *model;
    const hbi_model_adapter *adapter;
    const hbi_tokenizer_manager *tok_mgr;
    hbi_allocator *allocator;

    /* Session configuration */
    hbi_runtime_config config;

    /* Cancellation flag */
    _Atomic bool cancel_flag;

    /* Long-lived state */
    hbi_kv_manager *kv_manager;
    hbi_model_descriptor descriptor;
    hbi_model_context *model_ctx;
    hbi_context_handle *kv_ctx;

    /* Per-generate pipeline state */
    hbi_graph *graph;
    hbi_scheduler *scheduler;
    hbi_execution_plan *plan;
    hbi_memory_planner *planner;
    struct hbi_memory_plan *plan_memory;
    hbi_backend_manager *backend_mgr;

    /* RFC-020 Stream Engine */
    struct hbi_stream_engine *stream_engine;

    /* Execution state */
    struct hbi_exec_context *exec_ctx;
    uint32_t prefill_tokens;
    uint64_t total_tokens_generated;
} hbi_runtime_session;

void hbi_session_pipeline_reset(hbi_runtime_session *s);

#ifdef __cplusplus
}
#endif

#endif /* HB_RUNTIME_INTERNAL_H */
