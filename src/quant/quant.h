/* quant.h — Quantization format registry: pack/unpack contracts and format detection.
 *
 * Core-public header for the `quant` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_QUANT_H
#define HB_QUANT_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_quant_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_quant_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_QUANT_H */
