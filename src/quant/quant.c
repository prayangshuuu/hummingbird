/* quant.c — dtype decoding and dequantization to fp32 (RFC-015, DD-036).
 *
 * The single home for turning on-disk numeric formats into fp32 the reference
 * kernels consume. Correctness-first, scalar, no allocation. Native compressed
 * execution (MXFP4 kernels operating on packed data) is a later RFC and will
 * register against HBI_DTYPE_MXFP4 without changing this interface.
 */
#include "quant/quant_internal.h"

#include <string.h>

/* ── Half / bfloat expansion ─────────────────────────────────────────────── */

/* IEEE 754 binary16 → binary32. Handles subnormals, inf, and NaN. */
static float fp16_to_fp32(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t bits;

    if (exp == 0) {
        if (mant == 0) {
            bits = sign; /* signed zero */
        } else {
            /* Subnormal: normalize. */
            exp = 127u - 15u + 1u;
            while ((mant & 0x400u) == 0) {
                mant <<= 1;
                exp--;
            }
            mant &= 0x3FFu;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 0x1Fu) {
        bits = sign | 0x7F800000u | (mant << 13); /* inf / NaN */
    } else {
        exp = exp - 15u + 127u;
        bits = sign | (exp << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

/* bfloat16 → binary32: bf16 is just the top 16 bits of fp32. */
static float bf16_to_fp32(uint16_t b) {
    uint32_t bits = (uint32_t)b << 16;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

/* ── MXFP4 ────────────────────────────────────────────────────────────────── */

/* The 16 E2M1 code points (sign,exp2,mant1). Index = 4-bit code. The magnitude
 * table is the OCP FP4 (E2M1) value set {0,0.5,1,1.5,2,3,4,6}; the top bit is the
 * sign. */
static const float k_e2m1_lut[16] = {
    0.0f,  0.5f,  1.0f,  1.5f,  2.0f,  3.0f,  4.0f,  6.0f,
    -0.0f, -0.5f, -1.0f, -1.5f, -2.0f, -3.0f, -4.0f, -6.0f,
};

/* Decode an E8M0 scale byte to a float power of two. E8M0 encodes an unbiased
 * exponent with bias 127; the value is 2^(e-127). e==0xFF is reserved/NaN in the
 * OCP spec — we treat it as 0 scale defensively (never produced by valid data). */
static float e8m0_to_scale(uint8_t e) {
    if (e == 0xFFu) {
        return 0.0f;
    }
    int32_t unbiased = (int32_t)e - 127;
    /* 2^unbiased via bit construction, valid for the full E8M0 range. */
    uint32_t bits = (uint32_t)(unbiased + 127) << 23;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

hbi_status hbi_quant_mxfp4_validate(const hbi_quant_meta *meta, size_t n_elems) {
    if (meta == NULL) {
        return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "mxfp4_validate: NULL meta");
    }
    if (meta->group_size != 32) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "mxfp4_validate: group_size %d != 32",
                            meta->group_size);
    }
    if (meta->scales == NULL) {
        return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "mxfp4_validate: NULL scales");
    }
    if (meta->scale_dtype != HBI_DTYPE_INT8) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "mxfp4_validate: scale_dtype %s != int8",
                            hbi_dtype_str(meta->scale_dtype));
    }
    size_t want = (n_elems + 31u) / 32u;
    if (meta->num_scales != want) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "mxfp4_validate: num_scales %zu != %zu",
                            meta->num_scales, want);
    }
    return HBI_OK;
}

/* Decode MXFP4: `src` holds packed nibbles (2 elements/byte, low nibble first),
 * `meta->scales` holds one E8M0 byte per 32-element block. */
static hbi_status dequant_mxfp4(const uint8_t *src, size_t n_elems, const hbi_quant_meta *meta,
                                float *dst) {
    hbi_status st = hbi_quant_mxfp4_validate(meta, n_elems);
    if (st != HBI_OK) {
        return st;
    }
    const uint8_t *scales = (const uint8_t *)meta->scales;
    bool high_first = (meta->nibble_order == HBI_NIBBLE_HIGH_FIRST);

    for (size_t i = 0; i < n_elems; i++) {
        uint8_t byte = src[i >> 1];
        uint8_t nib;
        bool is_high = (i & 1u) != 0;
        if (high_first) {
            is_high = !is_high;
        }
        nib = is_high ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0Fu);

        float scale = e8m0_to_scale(scales[i >> 5]); /* block of 32 */
        dst[i] = k_e2m1_lut[nib] * scale;
    }
    return HBI_OK;
}

/* ── Public entry point ──────────────────────────────────────────────────── */

hbi_status hbi_quant_dequantize_to_fp32(hbi_dtype src_dtype, const void *src, size_t n_elems,
                                        const hbi_quant_meta *meta, float *dst) {
    if (src == NULL || dst == NULL || n_elems == 0) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "dequant: NULL src/dst or zero elems");
    }
    switch (src_dtype) {
    case HBI_DTYPE_FP32:
        memcpy(dst, src, n_elems * sizeof(float));
        return HBI_OK;
    case HBI_DTYPE_FP16: {
        const uint16_t *s = (const uint16_t *)src;
        for (size_t i = 0; i < n_elems; i++) {
            dst[i] = fp16_to_fp32(s[i]);
        }
        return HBI_OK;
    }
    case HBI_DTYPE_BF16: {
        const uint16_t *s = (const uint16_t *)src;
        for (size_t i = 0; i < n_elems; i++) {
            dst[i] = bf16_to_fp32(s[i]);
        }
        return HBI_OK;
    }
    case HBI_DTYPE_MXFP4:
        return dequant_mxfp4((const uint8_t *)src, n_elems, meta, dst);
    default:
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "dequant: unsupported dtype %s",
                            hbi_dtype_str(src_dtype));
    }
}

/* ── Module identity / self-test ─────────────────────────────────────────── */

const char *hbi_quant_name(void) {
    return "quant";
}

hbi_status hbi_quant_selftest(void) {
    /* fp32 passthrough. */
    float in[4] = {1.0f, -2.5f, 0.0f, 42.0f};
    float out[4] = {0};
    hbi_status st = hbi_quant_dequantize_to_fp32(HBI_DTYPE_FP32, in, 4, NULL, out);
    if (st != HBI_OK || out[0] != 1.0f || out[3] != 42.0f) {
        return HBI_ERR_INTERNAL;
    }
    /* bf16(1.0) == 0x3F80. */
    uint16_t b = 0x3F80u;
    float bf = 0.0f;
    st = hbi_quant_dequantize_to_fp32(HBI_DTYPE_BF16, &b, 1, NULL, &bf);
    if (st != HBI_OK || bf != 1.0f) {
        return HBI_ERR_INTERNAL;
    }
    return HBI_OK;
}
