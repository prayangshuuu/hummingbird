/* runtime.h — Orchestrator: owns the forward loop, sequences layers, drives the scheduler, produces
 * logits.
 *
 * Core-public header for the `runtime` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_RUNTIME_H
#define HB_RUNTIME_H

#include "common/common.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum hbi_sampler_type {
    HBI_SAMPLER_GREEDY = 0,
    HBI_SAMPLER_TOP_K = 1,
    HBI_SAMPLER_TOP_P = 2
} hbi_sampler_type;

typedef struct hbi_runtime_config {
    uint32_t max_new_tokens;
    bool greedy;
    uint32_t eos_token_id;
    hbi_sampler_type sampler; // Added for HBI_SAMPLER_GREEDY etc.
} hbi_runtime_config;

/* Forward declarations */
typedef struct hbi_load_session hbi_load_session;
typedef struct hbi_allocator hbi_allocator;
typedef struct hbi_runtime_session hbi_runtime_session;
typedef struct hbi_model_adapter hbi_model_adapter;
typedef struct hbi_tokenizer_manager hbi_tokenizer_manager;

hbi_status hbi_runtime_session_create(const hbi_load_session *model,
                                      const hbi_model_adapter *adapter,
                                      const hbi_tokenizer_manager *tok_mgr,
                                      const hbi_runtime_config *config, hbi_allocator *allocator,
                                      hbi_runtime_session **out_session);

void hbi_runtime_session_destroy(hbi_runtime_session *s);
hbi_status hbi_session_cancel(hbi_runtime_session *s);
hbi_status hbi_session_reset(hbi_runtime_session *s);

/* Human-readable module name. Never NULL. */
const char *hbi_runtime_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_runtime_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_RUNTIME_H */
