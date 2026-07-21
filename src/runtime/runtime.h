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

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_runtime_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_runtime_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_RUNTIME_H */
