/* model.h — Model adapter: descriptor + forward graph + tokenizer ref + quant classes. New models
 * are additive.
 *
 * Core-public header for the `model` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_MODEL_H
#define HB_MODEL_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_model_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_model_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_MODEL_H */
