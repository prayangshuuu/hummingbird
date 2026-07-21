/* tokenizer.h — Tokenizer adapter interface (BPE/unigram/...); encode/decode.
 *
 * Core-public header for the `tokenizer` module. Other modules include this;
 * external embedders use <hummingbird.h> instead. Symbols are prefixed
 * `hbi_` (internal, no stability guarantee). See docs/architecture.
 */
#ifndef HB_TOKENIZER_H
#define HB_TOKENIZER_H

#include "common/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human-readable module name. Never NULL. */
const char *hbi_tokenizer_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_tokenizer_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_TOKENIZER_H */
