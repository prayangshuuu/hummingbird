/* pipeline.h — Build the computation graph and execution plan (RFC-019).
 *
 * The pipeline takes the adapter's forward-pass description and compiles it
 * into an hbi_execution_plan that the executor can dispatch.
 *
 * Internal to the `runtime` module.
 */
#ifndef HB_RUNTIME_PIPELINE_H
#define HB_RUNTIME_PIPELINE_H

#include "runtime/runtime_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the computation graph from the adapter, compile it into an execution
 * plan, and store both in s->graph and s->plan.
 *
 * Also creates s->scheduler and s->backend_mgr if they do not yet exist.
 *
 * Requires s->adapter != NULL and s->descriptor to be filled (done in
 * session_create).  If either is missing, returns HBI_ERR_STATE.
 *
 * On failure the session's graph/plan/scheduler are left consistent:
 * any partially created objects are destroyed. */
hbi_status hbi_runtime_pipeline_build(hbi_runtime_session *s);

#ifdef __cplusplus
}
#endif

#endif /* HB_RUNTIME_PIPELINE_H */
