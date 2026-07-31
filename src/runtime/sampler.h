#ifndef HBI_SAMPLER_H
#define HBI_SAMPLER_H

#include "hummingbird/hummingbird.h"
#include "tensor/tensor.h"

/* Samples the next token ID from a logits tensor.
 * logits: 1D or 2D fp32 tensor.
 * out_token: receives the sampled token ID.
 */
hbi_status hbi_sampler_greedy(const hbi_tensor *logits, uint32_t *out_token);

#endif /* HBI_SAMPLER_H */
