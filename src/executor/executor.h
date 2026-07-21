/* executor.h — Walks a forward graph and dispatches each op node to its typed module on the active
 * backend.
 *
 * Core-public header for the `executor` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_EXECUTOR_H
#define HB_EXECUTOR_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_executor_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_executor_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_EXECUTOR_H */
