/* kv.h — KV-cache abstraction: MHA/GQA and compressed-latent (MLA) layouts; save/restore.
 *
 * Core-public header for the `kv` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_KV_H
#define HB_KV_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_kv_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_kv_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_KV_H */
