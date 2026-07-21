/* stream.h — Weight streaming: contiguity to single coalesced read to zero-copy views (hard
 * invariant).
 *
 * Core-public header for the `stream` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_STREAM_H
#define HB_STREAM_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_stream_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_stream_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_STREAM_H */
