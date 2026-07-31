/* generate.h — Prefill + decode generation loop (RFC-019).
 *
 * Internal to the `runtime` module.
 */
#ifndef HB_RUNTIME_GENERATE_H
#define HB_RUNTIME_GENERATE_H

#include "runtime/runtime_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Run the full prefill + autoregressive decode loop for `prompt`.
 * See runtime.h for the full contract. */
hbi_status hbi_runtime_generate(hbi_runtime_session *s, const char *prompt,
                                void (*on_text)(const char *text, void *ud), void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* HB_RUNTIME_GENERATE_H */
