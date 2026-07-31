/* session.h — Runtime session creation and destruction (RFC-019). */
#ifndef HB_RUNTIME_SESSION_H
#define HB_RUNTIME_SESSION_H

#include "runtime/runtime_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal helpers used by generate.c and pipeline.c. */

/* Tear down and free all session-owned pipeline state (graph, plan,
 * scheduler, backend_mgr) without destroying the session struct itself.
 * Called at the start of each generate() call to reclaim the previous run's
 * resources, and on session_destroy. */
void hbi_session_pipeline_reset(hbi_runtime_session *s);

#ifdef __cplusplus
}
#endif

#endif /* HB_RUNTIME_SESSION_H */
