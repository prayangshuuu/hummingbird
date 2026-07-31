/* executor.h — Walk the execution plan and dispatch backend commands (RFC-019).
 *
 * Internal to the `runtime` module.
 */
#ifndef HB_RUNTIME_EXECUTOR_H
#define HB_RUNTIME_EXECUTOR_H

#include "runtime/runtime_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Execute the compiled plan stored in s->plan.
 *
 * For each task in topological order the executor:
 *   1. Selects a registered backend (first available in the registry).
 *   2. Obtains a backend context from s->backend_mgr.
 *   3. Issues an HBI_CMD_KERNEL_DISPATCH command via backend->execute().
 *   4. After all tasks: calls backend->sync() to flush asynchronous work.
 *
 * Requires s->plan != NULL, s->backend_mgr != NULL.
 * Returns HBI_ERR_STATE if no backend is registered.
 * Returns HBI_ERR_UNSUPPORTED if the backend does not implement execute(). */
hbi_status hbi_runtime_executor_run(hbi_runtime_session *s);

#ifdef __cplusplus
}
#endif

#endif /* HB_RUNTIME_EXECUTOR_H */
