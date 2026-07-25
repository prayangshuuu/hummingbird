/* quant.h — Quantization module: dtype decoding and dequantization (RFC-015, DD-036).
 *
 * Core-public header for the `quant` module (layer 4). This is the SINGLE home
 * for all dtype decoding and dequantization. Architecture constraints (DD-036):
 *   - The Model Loader must NOT know quantization details.
 *   - This module performs ALL decoding/dequant.
 *   - The Tensor Runtime stores quant metadata (hbi_quant_meta in tensor.h).
 *   - The Kernel Runtime stays dtype-agnostic.
 *   - Future native MXFP4 kernels must need no API redesign.
 *
 * Supported conversions (all produce fp32):
 *   fp32  → fp32  (passthrough copy)
 *   fp16  → fp32  (IEEE 754 half-precision expansion)
 *   bf16  → fp32  (bfloat16 expansion: sign+exp shared, mantissa zero-padded)
 *   mxfp4 → fp32  (OCP MXFP4: E2M1 elements packed 2/byte, one E8M0 scale per
 *                  32-element block along the last axis)
 */
#ifndef HB_QUANT_H
#define HB_QUANT_H

#include "common/common.h"
#include "tensor/tensor.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Dequantization ──────────────────────────────────────────────────────────
 *
 * hbi_quant_dequantize_to_fp32 — decode `n_elems` elements from `src` (dtype
 * `src_dtype`) into `dst` (caller-allocated fp32 array of at least `n_elems`
 * floats).
 *
 * For HBI_DTYPE_MXFP4, `meta` must be non-NULL and carry:
 *   meta->group_size  == 32
 *   meta->scales      != NULL  (E8M0 bytes, one per block)
 *   meta->scale_dtype == HBI_DTYPE_INT8  (raw E8M0 byte)
 *   meta->num_scales  == ceil(n_elems / 32)
 *
 * For all other dtypes, `meta` is ignored (may be NULL).
 *
 * Fails HBI_ERR_INVALID_ARG (NULL src/dst, n_elems==0, unsupported dtype),
 * HBI_ERR_CORRUPT (MXFP4 meta missing/invalid). Never allocates. */
hbi_status hbi_quant_dequantize_to_fp32(hbi_dtype src_dtype, const void *src, size_t n_elems,
                                        const hbi_quant_meta *meta, float *dst);

/* ── MXFP4 validation ────────────────────────────────────────────────────────
 *
 * Validate that `meta` is consistent with an MXFP4 tensor of `n_elems` elements.
 * Checks: group_size==32, scales non-NULL, scale_dtype==INT8,
 * num_scales==ceil(n_elems/32). */
hbi_status hbi_quant_mxfp4_validate(const hbi_quant_meta *meta, size_t n_elems);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_quant_name(void);
hbi_status hbi_quant_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_QUANT_H */
