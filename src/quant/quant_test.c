/* quant_test.c — unit tests for dtype decoding / dequantization to fp32. */
#include "quant/quant.h"

#include "hbi_test.h"

#include <math.h>
#include <string.h>

static bool approx(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

static void test_fp32_passthrough(void) {
    float in[3] = {1.5f, -0.25f, 100.0f};
    float out[3] = {0};
    HBI_CHECK_EQ_INT(hbi_quant_dequantize_to_fp32(HBI_DTYPE_FP32, in, 3, NULL, out), HBI_OK);
    HBI_CHECK(out[0] == 1.5f && out[1] == -0.25f && out[2] == 100.0f);
}

static void test_bf16(void) {
    /* bf16 = top 16 bits of fp32. 1.0f=0x3F800000 → 0x3F80; -2.0f=0xC0000000 → 0xC000. */
    uint16_t in[3] = {0x3F80u, 0xC000u, 0x0000u};
    float out[3] = {0};
    HBI_CHECK_EQ_INT(hbi_quant_dequantize_to_fp32(HBI_DTYPE_BF16, in, 3, NULL, out), HBI_OK);
    HBI_CHECK(out[0] == 1.0f);
    HBI_CHECK(out[1] == -2.0f);
    HBI_CHECK(out[2] == 0.0f);
}

static void test_fp16(void) {
    /* fp16: 1.0=0x3C00, 0.5=0x3800, -2.0=0xC000, 2.0=0x4000. */
    uint16_t in[4] = {0x3C00u, 0x3800u, 0xC000u, 0x4000u};
    float out[4] = {0};
    HBI_CHECK_EQ_INT(hbi_quant_dequantize_to_fp32(HBI_DTYPE_FP16, in, 4, NULL, out), HBI_OK);
    HBI_CHECK(approx(out[0], 1.0f, 1e-6f));
    HBI_CHECK(approx(out[1], 0.5f, 1e-6f));
    HBI_CHECK(approx(out[2], -2.0f, 1e-6f));
    HBI_CHECK(approx(out[3], 2.0f, 1e-6f));
}

static void test_fp16_subnormal_inf(void) {
    /* Smallest positive subnormal = 2^-24 ≈ 5.96e-8 (0x0001); +inf = 0x7C00. */
    uint16_t in[2] = {0x0001u, 0x7C00u};
    float out[2] = {0};
    HBI_CHECK_EQ_INT(hbi_quant_dequantize_to_fp32(HBI_DTYPE_FP16, in, 2, NULL, out), HBI_OK);
    HBI_CHECK(approx(out[0], ldexpf(1.0f, -24), 1e-12f));
    HBI_CHECK(isinf(out[1]) && out[1] > 0.0f);
}

/* Build a single 32-element MXFP4 block: all nibbles = code 2 (value 1.0),
 * packed low-first (two per byte → 0x22), scale = 2^1 (E8M0 byte = 128). */
static void test_mxfp4_block(void) {
    uint8_t packed[16];
    memset(packed, 0x22, sizeof(packed)); /* each nibble = 2 → 1.0 */
    uint8_t scale = 128u;                 /* 2^(128-127) = 2.0 */

    hbi_quant_meta meta;
    memset(&meta, 0, sizeof(meta));
    meta.scheme = HBI_QUANT_SCHEME_AFFINE_SYM;
    meta.group_size = 32;
    meta.group_axis = 0;
    meta.scales = &scale;
    meta.scale_dtype = HBI_DTYPE_INT8;
    meta.num_scales = 1;
    meta.nibble_order = HBI_NIBBLE_LOW_FIRST;

    float out[32] = {0};
    HBI_CHECK_EQ_INT(hbi_quant_dequantize_to_fp32(HBI_DTYPE_MXFP4, packed, 32, &meta, out), HBI_OK);
    for (int i = 0; i < 32; i++) {
        HBI_CHECK(approx(out[i], 2.0f, 1e-6f)); /* 1.0 * 2.0 */
    }
}

/* Mixed nibble codes within a block: codes 0,1,2,3 → 0,0.5,1,1.5; scale 2^0=1. */
static void test_mxfp4_values(void) {
    uint8_t packed[16];
    /* low nibble first: byte = (high<<4)|low. Pairs: (0,1),(2,3) then zeros. */
    packed[0] = (uint8_t)((1u << 4) | 0u); /* elems 0,1 = code0,code1 */
    packed[1] = (uint8_t)((3u << 4) | 2u); /* elems 2,3 = code2,code3 */
    memset(packed + 2, 0x00, 14);          /* remaining = code0 = 0.0 */
    uint8_t scale = 127u;                  /* 2^0 = 1.0 */

    hbi_quant_meta meta;
    memset(&meta, 0, sizeof(meta));
    meta.group_size = 32;
    meta.scales = &scale;
    meta.scale_dtype = HBI_DTYPE_INT8;
    meta.num_scales = 1;
    meta.nibble_order = HBI_NIBBLE_LOW_FIRST;

    float out[32] = {0};
    HBI_CHECK_EQ_INT(hbi_quant_dequantize_to_fp32(HBI_DTYPE_MXFP4, packed, 32, &meta, out), HBI_OK);
    HBI_CHECK(approx(out[0], 0.0f, 1e-6f));
    HBI_CHECK(approx(out[1], 0.5f, 1e-6f));
    HBI_CHECK(approx(out[2], 1.0f, 1e-6f));
    HBI_CHECK(approx(out[3], 1.5f, 1e-6f));
}

static void test_mxfp4_validation_fails(void) {
    uint8_t scale = 127u;
    hbi_quant_meta meta;
    memset(&meta, 0, sizeof(meta));
    meta.group_size = 16; /* wrong */
    meta.scales = &scale;
    meta.scale_dtype = HBI_DTYPE_INT8;
    meta.num_scales = 1;
    HBI_CHECK(hbi_quant_mxfp4_validate(&meta, 32) != HBI_OK);

    meta.group_size = 32;
    meta.num_scales = 5; /* wrong: 32 elems → 1 block */
    HBI_CHECK(hbi_quant_mxfp4_validate(&meta, 32) != HBI_OK);

    meta.num_scales = 1;
    HBI_CHECK_EQ_INT(hbi_quant_mxfp4_validate(&meta, 32), HBI_OK);
}

static void test_bad_args(void) {
    float out[1] = {0};
    HBI_CHECK(hbi_quant_dequantize_to_fp32(HBI_DTYPE_FP32, NULL, 1, NULL, out) != HBI_OK);
    float in[1] = {1.0f};
    HBI_CHECK(hbi_quant_dequantize_to_fp32(HBI_DTYPE_FP32, in, 0, NULL, out) != HBI_OK);
    HBI_CHECK(hbi_quant_dequantize_to_fp32(HBI_DTYPE_INT8, in, 1, NULL, out) != HBI_OK);
}

static void test_int8_not_implemented(void) {
    int8_t in[1] = {5};
    float out[1] = {0};

    hbi_quant_meta meta;
    memset(&meta, 0, sizeof(meta));
    meta.scheme = HBI_QUANT_SCHEME_AFFINE_SYM;
    meta.group_size = 32;

    hbi_status st = hbi_quant_dequantize_to_fp32(HBI_DTYPE_INT8, in, 1, &meta, out);
    HBI_CHECK(st == HBI_ERR_UNSUPPORTED);
}

static void test_selftest(void) {
    HBI_CHECK_EQ_INT(hbi_quant_selftest(), HBI_OK);
}

int main(void) {
    HBI_TEST_BEGIN("quant");
    HBI_RUN(test_fp32_passthrough);
    HBI_RUN(test_bf16);
    HBI_RUN(test_fp16);
    HBI_RUN(test_fp16_subnormal_inf);
    HBI_RUN(test_mxfp4_block);
    HBI_RUN(test_mxfp4_values);
    HBI_RUN(test_mxfp4_validation_fails);
    HBI_RUN(test_bad_args);
    HBI_RUN(test_int8_not_implemented);
    HBI_RUN(test_selftest);
    return HBI_TEST_END();
}
