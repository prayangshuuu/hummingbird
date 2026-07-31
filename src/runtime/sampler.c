#include "sampler.h"

hbi_status hbi_sampler_greedy(const hbi_tensor *logits, uint32_t *out_token) {
    if (!logits || !out_token)
        return HBI_ERR_INVALID_ARG;

    int64_t num_elements = 0;
    hbi_status st = hbi_shape_elem_count(hbi_tensor_shape(logits), &num_elements);
    if (st != HBI_OK)
        return st;

    if (num_elements == 0) {
        return HBI_ERR_INVALID_ARG;
    }

    if (hbi_tensor_dtype(logits) != HBI_DTYPE_FP32) {
        return HBI_ERR_UNSUPPORTED;
    }

    const float *data = (const float *)hbi_tensor_cdata(logits);
    if (!data)
        return HBI_ERR_INVALID_ARG;

    const hbi_shape *shape = hbi_tensor_shape(logits);
    int64_t vocab_size = num_elements;
    if (shape->rank == 2) {
        vocab_size = shape->dims[1];
    } else if (shape->rank > 2) {
        vocab_size = shape->dims[shape->rank - 1];
    }

    /* We only care about the last sequence item for generation */
    const float *last_row = data + (num_elements - vocab_size);

    float max_val = last_row[0];
    uint32_t max_idx = 0;

    for (int64_t i = 1; i < vocab_size; i++) {
        if (last_row[i] > max_val) {
            max_val = last_row[i];
            max_idx = (uint32_t)i;
        }
    }

    *out_token = max_idx;
    return HBI_OK;
}
