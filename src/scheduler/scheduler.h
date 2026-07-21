/* scheduler.h — Overlaps I/O with compute and prefetches (PIPE/PILOT). Speculative actions never
 * change output.
 *
 * Core-public header for the `scheduler` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_SCHEDULER_H
#define HB_SCHEDULER_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_scheduler_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_scheduler_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_SCHEDULER_H */
