/* context.h — Per-session decode context: KV state, sampling state, run mode.
 *
 * Core-public header for the `context` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_CONTEXT_H
#define HB_CONTEXT_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_context_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_context_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_CONTEXT_H */
