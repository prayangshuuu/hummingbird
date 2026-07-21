/* tensor_test.c — unit tests for the `tensor` data-model module.
 *
 * Covers: dtype queries + packed sizing, shape init/scalar/equal/broadcast,
 * contiguous strides, tensor alloc/wrap/clone/copy/move lifecycle, zero-copy
 * views (view/reshape/slice/subtensor/transpose/permute/squeeze/unsqueeze),
 * quant metadata attach/validate, full-tensor validation, and alignment. No
 * kernels or inference logic — this is the foundation data model only.
 */
#include "tensor/tensor.h"

#include "hbi_test.h"

#include <stdint.h>
#include <string.h>

/* ── dtypes ─────────────────────────────────────────────────────────────────── */

static void test_dtype_tables(void) {
    HBI_CHECK_STR_EQ(hbi_dtype_str(HBI_DTYPE_FP32), "fp32");
    HBI_CHECK_STR_EQ(hbi_dtype_str(HBI_DTYPE_INT4), "int4");
    HBI_CHECK_STR_EQ(hbi_dtype_str(HBI_DTYPE_INVALID), "invalid");

    HBI_CHECK_EQ_INT(hbi_dtype_bits(HBI_DTYPE_FP32), 32);
    HBI_CHECK_EQ_INT(hbi_dtype_bits(HBI_DTYPE_BF16), 16);
    HBI_CHECK_EQ_INT(hbi_dtype_bits(HBI_DTYPE_INT8), 8);
    HBI_CHECK_EQ_INT(hbi_dtype_bits(HBI_DTYPE_INT4), 4);
    HBI_CHECK_EQ_INT(hbi_dtype_bits(HBI_DTYPE_INT2), 2);

    HBI_CHECK(hbi_dtype_is_valid(HBI_DTYPE_FP32));
    HBI_CHECK(!hbi_dtype_is_valid(HBI_DTYPE_INVALID));
    HBI_CHECK(!hbi_dtype_is_valid(HBI_DTYPE_COUNT));

    HBI_CHECK(hbi_dtype_is_reserved(HBI_DTYPE_FP8));
    HBI_CHECK(hbi_dtype_is_reserved(HBI_DTYPE_NF4));
    HBI_CHECK(!hbi_dtype_is_reserved(HBI_DTYPE_INT8));

    HBI_CHECK(hbi_dtype_is_sub_byte(HBI_DTYPE_INT4));
    HBI_CHECK(hbi_dtype_is_sub_byte(HBI_DTYPE_INT2));
    HBI_CHECK(!hbi_dtype_is_sub_byte(HBI_DTYPE_INT8));
}

static void test_packed_nbytes(void) {
    size_t nb = 0;
    /* 10 fp32 = 40 bytes. */
    HBI_CHECK(hbi_dtype_packed_nbytes(HBI_DTYPE_FP32, 10, &nb) == HBI_OK);
    HBI_CHECK_EQ_INT(nb, 40);
    /* 7 int4 = ceil(28/8) = 4 bytes (sub-byte packing must not truncate). */
    HBI_CHECK(hbi_dtype_packed_nbytes(HBI_DTYPE_INT4, 7, &nb) == HBI_OK);
    HBI_CHECK_EQ_INT(nb, 4);
    /* 3 int2 = ceil(6/8) = 1 byte. */
    HBI_CHECK(hbi_dtype_packed_nbytes(HBI_DTYPE_INT2, 3, &nb) == HBI_OK);
    HBI_CHECK_EQ_INT(nb, 1);
    /* Errors: NULL out, bad dtype, negative count. */
    HBI_CHECK(hbi_dtype_packed_nbytes(HBI_DTYPE_FP32, 10, NULL) == HBI_ERR_INVALID_ARG);
    HBI_CHECK(hbi_dtype_packed_nbytes(HBI_DTYPE_INVALID, 10, &nb) == HBI_ERR_INVALID_ARG);
    HBI_CHECK(hbi_dtype_packed_nbytes(HBI_DTYPE_FP32, -1, &nb) == HBI_ERR_INVALID_ARG);
}

/* ── shapes ─────────────────────────────────────────────────────────────────── */

static void test_shape_basic(void) {
    int64_t dims[3] = {2, 3, 4};
    hbi_shape s;
    HBI_CHECK(hbi_shape_init(&s, dims, 3) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_shape_rank(&s), 3);
    int64_t n = 0;
    HBI_CHECK(hbi_shape_elem_count(&s, &n) == HBI_OK);
    HBI_CHECK_EQ_INT(n, 24);

    hbi_shape sc;
    HBI_CHECK(hbi_shape_init_scalar(&sc) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_shape_rank(&sc), 0);
    HBI_CHECK(hbi_shape_elem_count(&sc, &n) == HBI_OK);
    HBI_CHECK_EQ_INT(n, 1); /* scalar → 1 element */

    /* Bad rank and non-positive dims are rejected. */
    HBI_CHECK(hbi_shape_init(&s, dims, HBI_TENSOR_MAX_RANK + 1) == HBI_ERR_INVALID_ARG);
    int64_t bad[2] = {2, 0};
    HBI_CHECK(hbi_shape_init(&s, bad, 2) == HBI_ERR_INVALID_ARG);
    HBI_CHECK_EQ_INT(hbi_shape_rank(NULL), 0);
}

static void test_shape_equal_broadcast(void) {
    int64_t a[2] = {3, 4}, b[2] = {3, 4}, c[2] = {3, 5};
    hbi_shape sa, sb, sc;
    hbi_shape_init(&sa, a, 2);
    hbi_shape_init(&sb, b, 2);
    hbi_shape_init(&sc, c, 2);
    HBI_CHECK(hbi_shape_equal(&sa, &sb));
    HBI_CHECK(!hbi_shape_equal(&sa, &sc));
    HBI_CHECK(hbi_shape_equal(NULL, NULL));
    HBI_CHECK(!hbi_shape_equal(&sa, NULL));

    /* Broadcast: (3,4) vs (4,) → compatible → (3,4). (3,4) vs (3,) → not. */
    int64_t d[1] = {4}, e[1] = {3};
    hbi_shape sd, se, out;
    hbi_shape_init(&sd, d, 1);
    hbi_shape_init(&se, e, 1);
    HBI_CHECK(hbi_shape_broadcast_compatible(&sa, &sd));
    HBI_CHECK(!hbi_shape_broadcast_compatible(&sa, &se));
    HBI_CHECK(hbi_shape_broadcast(&sa, &sd, &out) == HBI_OK);
    HBI_CHECK_EQ_INT(out.rank, 2);
    HBI_CHECK_EQ_INT(out.dims[0], 3);
    HBI_CHECK_EQ_INT(out.dims[1], 4);
    /* A size-1 axis broadcasts up. */
    int64_t f[2] = {1, 4};
    hbi_shape sf;
    hbi_shape_init(&sf, f, 2);
    HBI_CHECK(hbi_shape_broadcast(&sa, &sf, &out) == HBI_OK);
    HBI_CHECK_EQ_INT(out.dims[0], 3);
    HBI_CHECK(hbi_shape_broadcast(&sa, &se, &out) == HBI_ERR_INVALID_ARG);
}

/* ── strides ────────────────────────────────────────────────────────────────── */

static void test_strides_contiguous(void) {
    int64_t dims[3] = {2, 3, 4};
    hbi_shape s;
    hbi_shape_init(&s, dims, 3);
    hbi_strides st;
    HBI_CHECK(hbi_strides_init_contiguous(&st, &s) == HBI_OK);
    /* Row-major: [12, 4, 1]. */
    HBI_CHECK_EQ_INT(st.stride[0], 12);
    HBI_CHECK_EQ_INT(st.stride[1], 4);
    HBI_CHECK_EQ_INT(st.stride[2], 1);
    HBI_CHECK(hbi_strides_is_contiguous(&st, &s));
    HBI_CHECK(hbi_strides_validate(&st, &s) == HBI_OK);

    /* A permuted stride set is valid but not contiguous. */
    hbi_strides bad = st;
    bad.stride[0] = 1;
    bad.stride[2] = 12;
    HBI_CHECK(!hbi_strides_is_contiguous(&bad, &s));
    /* Negative strides are rejected by validate. */
    hbi_strides neg = st;
    neg.stride[0] = -1;
    HBI_CHECK(hbi_strides_validate(&neg, &s) == HBI_ERR_INVALID_ARG);
}

/* ── tensor lifecycle ───────────────────────────────────────────────────────── */

static void test_alloc_validate_destroy(void) {
    int64_t dims[2] = {4, 8};
    hbi_shape s;
    hbi_shape_init(&s, dims, 2);
    hbi_tensor t;
    HBI_CHECK(hbi_tensor_alloc(&t, HBI_DTYPE_FP32, &s) == HBI_OK);
    HBI_CHECK(hbi_tensor_owns_data(&t));
    HBI_CHECK(hbi_tensor_is_contiguous(&t));
    HBI_CHECK(!hbi_tensor_is_readonly(&t));
    HBI_CHECK_EQ_INT(hbi_tensor_nbytes(&t), 4 * 8 * 4);
    HBI_CHECK(hbi_tensor_is_aligned(&t, HBI_TENSOR_DEFAULT_ALIGN));
    HBI_CHECK(hbi_tensor_data_mut(&t) != NULL);
    HBI_CHECK(hbi_tensor_validate(&t) == HBI_OK);
    hbi_tensor_destroy(&t);
    /* Destroy zeroes the struct and is idempotent / NULL-safe. */
    HBI_CHECK(hbi_tensor_cdata(&t) == NULL);
    hbi_tensor_destroy(&t);
    hbi_tensor_destroy(NULL);

    /* Reserved dtype is describable but not allocatable. */
    hbi_tensor r;
    HBI_CHECK(hbi_tensor_alloc(&r, HBI_DTYPE_FP8, &s) == HBI_ERR_UNSUPPORTED);
}

static void test_alloc_aligned(void) {
    int64_t dims[1] = {16};
    hbi_shape s;
    hbi_shape_init(&s, dims, 1);
    hbi_tensor t;
    HBI_CHECK(hbi_tensor_alloc_aligned(&t, HBI_DTYPE_FP32, &s, 256) == HBI_OK);
    HBI_CHECK(hbi_tensor_is_aligned(&t, 256));
    hbi_tensor_destroy(&t);
    /* Non-power-of-two alignment is rejected. */
    HBI_CHECK(hbi_tensor_alloc_aligned(&t, HBI_DTYPE_FP32, &s, 100) == HBI_ERR_INVALID_ARG);
}

static void test_wrap(void) {
    int64_t dims[2] = {2, 2};
    hbi_shape s;
    hbi_shape_init(&s, dims, 2);
    float buf[4] = {1.0f, 2.0f, 3.0f, 4.0f};

    hbi_tensor t;
    HBI_CHECK(hbi_tensor_wrap(&t, HBI_DTYPE_FP32, &s, buf, sizeof(buf)) == HBI_OK);
    HBI_CHECK(!hbi_tensor_owns_data(&t));
    HBI_CHECK(hbi_tensor_data_mut(&t) == buf);
    HBI_CHECK(hbi_tensor_validate(&t) == HBI_OK);
    hbi_tensor_destroy(&t);      /* must NOT free buf */
    HBI_CHECK_EQ_INT(buf[0], 1); /* still valid */

    /* Read-only wrap denies mutable access. */
    hbi_tensor ro;
    HBI_CHECK(hbi_tensor_wrap_readonly(&ro, HBI_DTYPE_FP32, &s, buf, sizeof(buf)) == HBI_OK);
    HBI_CHECK(hbi_tensor_is_readonly(&ro));
    HBI_CHECK(hbi_tensor_data_mut(&ro) == NULL);
    HBI_CHECK(hbi_tensor_cdata(&ro) == buf);
    hbi_tensor_destroy(&ro);

    /* Too-small buffer is rejected. */
    hbi_tensor bad;
    HBI_CHECK(hbi_tensor_wrap(&bad, HBI_DTYPE_FP32, &s, buf, 4) == HBI_ERR_INVALID_ARG);
}

static void test_clone_copy_move(void) {
    int64_t dims[2] = {2, 3};
    hbi_shape s;
    hbi_shape_init(&s, dims, 2);
    hbi_tensor src;
    hbi_tensor_alloc(&src, HBI_DTYPE_FP32, &s);
    float *sd = (float *)hbi_tensor_data_mut(&src);
    for (int i = 0; i < 6; ++i) {
        sd[i] = (float)(i + 1);
    }

    /* clone → deep copy, independent buffer, equal bytes. */
    hbi_tensor cl;
    HBI_CHECK(hbi_tensor_clone(&src, &cl) == HBI_OK);
    HBI_CHECK(hbi_tensor_owns_data(&cl));
    HBI_CHECK(hbi_tensor_cdata(&cl) != hbi_tensor_cdata(&src));
    HBI_CHECK(memcmp(hbi_tensor_cdata(&cl), hbi_tensor_cdata(&src), src.nbytes) == 0);

    /* copy_into an existing tensor of same dtype/shape. */
    hbi_tensor dst;
    hbi_tensor_alloc(&dst, HBI_DTYPE_FP32, &s);
    HBI_CHECK(hbi_tensor_copy_into(&src, &dst) == HBI_OK);
    HBI_CHECK(memcmp(hbi_tensor_cdata(&dst), hbi_tensor_cdata(&src), src.nbytes) == 0);
    /* shape mismatch → rejected. */
    int64_t d2[1] = {6};
    hbi_shape s2;
    hbi_shape_init(&s2, d2, 1);
    hbi_tensor dst2;
    hbi_tensor_alloc(&dst2, HBI_DTYPE_FP32, &s2);
    HBI_CHECK(hbi_tensor_copy_into(&src, &dst2) == HBI_ERR_INVALID_ARG);

    /* move → dst takes buffer, src zeroed. */
    const void *orig = hbi_tensor_cdata(&cl);
    hbi_tensor moved;
    memset(&moved, 0, sizeof(moved));
    HBI_CHECK(hbi_tensor_move(&moved, &cl) == HBI_OK);
    HBI_CHECK(hbi_tensor_cdata(&moved) == orig);
    HBI_CHECK(hbi_tensor_cdata(&cl) == NULL);
    HBI_CHECK(!hbi_tensor_owns_data(&cl));

    hbi_tensor_destroy(&src);
    hbi_tensor_destroy(&dst);
    hbi_tensor_destroy(&dst2);
    hbi_tensor_destroy(&moved);
}

/* Copy-into across two non-contiguous layouts must respect strides. */
static void test_copy_into_strided(void) {
    int64_t dims[2] = {2, 3};
    hbi_shape s;
    hbi_shape_init(&s, dims, 2);
    hbi_tensor src;
    hbi_tensor_alloc(&src, HBI_DTYPE_FP32, &s);
    float *sd = (float *)hbi_tensor_data_mut(&src);
    for (int i = 0; i < 6; ++i) {
        sd[i] = (float)i;
    }
    /* transpose view of src (non-contiguous), clone materializes it contiguous. */
    hbi_tensor tv;
    HBI_CHECK(hbi_tensor_transpose(&src, 0, 1, &tv) == HBI_OK);
    HBI_CHECK(!hbi_tensor_is_contiguous(&tv));
    hbi_tensor mat;
    HBI_CHECK(hbi_tensor_clone(&tv, &mat) == HBI_OK);
    HBI_CHECK(hbi_tensor_is_contiguous(&mat));
    /* mat is 3x2; element [i][j] == src[j][i]. */
    const float *md = (const float *)hbi_tensor_cdata(&mat);
    /* mat[0][0]=src[0][0]=0, mat[0][1]=src[1][0]=3, mat[1][0]=src[0][1]=1. */
    HBI_CHECK_EQ_INT((int)md[0], 0);
    HBI_CHECK_EQ_INT((int)md[1], 3);
    HBI_CHECK_EQ_INT((int)md[2], 1);
    hbi_tensor_destroy(&src);
    hbi_tensor_destroy(&mat);
}

/* ── views ──────────────────────────────────────────────────────────────────── */

static void test_reshape(void) {
    int64_t dims[2] = {2, 6};
    hbi_shape s;
    hbi_shape_init(&s, dims, 2);
    hbi_tensor t;
    hbi_tensor_alloc(&t, HBI_DTYPE_FP32, &s);

    int64_t nd[3] = {2, 3, 2};
    hbi_shape ns;
    hbi_shape_init(&ns, nd, 3);
    hbi_tensor v;
    HBI_CHECK(hbi_tensor_reshape(&t, &ns, &v) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&v)->rank, 3);
    HBI_CHECK(hbi_tensor_is_contiguous(&v));
    HBI_CHECK(hbi_tensor_cdata(&v) == hbi_tensor_cdata(&t)); /* shares buffer */
    HBI_CHECK(!hbi_tensor_owns_data(&v));

    /* Element-count mismatch → rejected. */
    int64_t wd[1] = {5};
    hbi_shape ws;
    hbi_shape_init(&ws, wd, 1);
    hbi_tensor bad;
    HBI_CHECK(hbi_tensor_reshape(&t, &ws, &bad) == HBI_ERR_INVALID_ARG);
    hbi_tensor_destroy(&t);
}

static void test_slice_subtensor(void) {
    int64_t dims[2] = {4, 5};
    hbi_shape s;
    hbi_shape_init(&s, dims, 2);
    hbi_tensor t;
    hbi_tensor_alloc(&t, HBI_DTYPE_FP32, &s);
    float *td = (float *)hbi_tensor_data_mut(&t);
    for (int i = 0; i < 20; ++i) {
        td[i] = (float)i;
    }

    /* slice rows [1,3) → shape (2,5), data starts at row 1 (offset 5 floats). */
    hbi_tensor sl;
    HBI_CHECK(hbi_tensor_slice(&t, 0, 1, 2, &sl) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&sl)->dims[0], 2);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&sl)->dims[1], 5);
    HBI_CHECK_EQ_INT((int)((const float *)hbi_tensor_cdata(&sl))[0], 5);
    HBI_CHECK(hbi_tensor_validate(&sl) == HBI_OK);

    /* out-of-range slice → rejected. */
    hbi_tensor bad;
    HBI_CHECK(hbi_tensor_slice(&t, 0, 3, 2, &bad) == HBI_ERR_INVALID_ARG);
    HBI_CHECK(hbi_tensor_slice(&t, 2, 0, 1, &bad) == HBI_ERR_INVALID_ARG);

    /* subtensor: rows [1,3), cols [2,4) → (2,2), first element = t[1][2] = 7. */
    int64_t starts[2] = {1, 2}, counts[2] = {2, 2};
    hbi_tensor sub;
    HBI_CHECK(hbi_tensor_subtensor(&t, starts, counts, 2, &sub) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&sub)->dims[0], 2);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&sub)->dims[1], 2);
    HBI_CHECK_EQ_INT((int)((const float *)hbi_tensor_cdata(&sub))[0], 7);
    HBI_CHECK(!hbi_tensor_is_contiguous(&sub)); /* column slice breaks contiguity */
    HBI_CHECK(hbi_tensor_validate(&sub) == HBI_OK);
    hbi_tensor_destroy(&t);
}

static void test_transpose_permute(void) {
    int64_t dims[3] = {2, 3, 4};
    hbi_shape s;
    hbi_shape_init(&s, dims, 3);
    hbi_tensor t;
    hbi_tensor_alloc(&t, HBI_DTYPE_FP32, &s);

    hbi_tensor tr;
    HBI_CHECK(hbi_tensor_transpose(&t, 0, 2, &tr) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&tr)->dims[0], 4);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&tr)->dims[2], 2);
    HBI_CHECK(!hbi_tensor_is_contiguous(&tr));
    HBI_CHECK(hbi_tensor_validate(&tr) == HBI_OK);

    /* permute (2,0,1): dims → (4,2,3). */
    uint32_t perm[3] = {2, 0, 1};
    hbi_tensor pm;
    HBI_CHECK(hbi_tensor_permute(&t, perm, 3, &pm) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&pm)->dims[0], 4);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&pm)->dims[1], 2);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&pm)->dims[2], 3);
    HBI_CHECK(hbi_tensor_validate(&pm) == HBI_OK);

    /* non-bijective perm → rejected. */
    uint32_t badperm[3] = {0, 0, 1};
    hbi_tensor bad;
    HBI_CHECK(hbi_tensor_permute(&t, badperm, 3, &bad) == HBI_ERR_INVALID_ARG);
    hbi_tensor_destroy(&t);
}

static void test_squeeze_unsqueeze(void) {
    int64_t dims[4] = {1, 3, 1, 4};
    hbi_shape s;
    hbi_shape_init(&s, dims, 4);
    hbi_tensor t;
    hbi_tensor_alloc(&t, HBI_DTYPE_FP32, &s);

    hbi_tensor sq;
    HBI_CHECK(hbi_tensor_squeeze(&t, &sq) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&sq)->rank, 2);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&sq)->dims[0], 3);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&sq)->dims[1], 4);
    HBI_CHECK(hbi_tensor_validate(&sq) == HBI_OK);

    hbi_tensor un;
    HBI_CHECK(hbi_tensor_unsqueeze(&sq, 0, &un) == HBI_OK);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&un)->rank, 3);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&un)->dims[0], 1);
    HBI_CHECK_EQ_INT(hbi_tensor_shape(&un)->dims[1], 3);
    HBI_CHECK(hbi_tensor_validate(&un) == HBI_OK);
    hbi_tensor_destroy(&t);
}

static void test_subbyte_view_restrictions(void) {
    int64_t dims[2] = {4, 4};
    hbi_shape s;
    hbi_shape_init(&s, dims, 2);
    hbi_tensor t;
    HBI_CHECK(hbi_tensor_alloc(&t, HBI_DTYPE_INT4, &s) == HBI_OK);
    /* transpose of a sub-byte tensor is unsupported (can't stride a nibble). */
    hbi_tensor tr;
    HBI_CHECK(hbi_tensor_transpose(&t, 0, 1, &tr) == HBI_ERR_UNSUPPORTED);
    /* identity permute is allowed. */
    uint32_t idp[2] = {0, 1};
    hbi_tensor pm;
    HBI_CHECK(hbi_tensor_permute(&t, idp, 2, &pm) == HBI_OK);
    /* row slice is byte-aligned (4 int4 = 2 bytes/row) → allowed. */
    hbi_tensor sl;
    HBI_CHECK(hbi_tensor_slice(&t, 0, 1, 2, &sl) == HBI_OK);
    hbi_tensor_destroy(&t);
}

/* ── quantization metadata ──────────────────────────────────────────────────── */

static void test_quant_meta(void) {
    int64_t dims[2] = {4, 128};
    hbi_shape s;
    hbi_shape_init(&s, dims, 2);
    hbi_tensor t;
    hbi_tensor_alloc(&t, HBI_DTYPE_INT4, &s);
    HBI_CHECK(!hbi_tensor_is_quantized(&t));
    HBI_CHECK(hbi_tensor_quant(&t) == NULL);

    static const float scales[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    hbi_quant_meta m;
    HBI_CHECK(hbi_quant_meta_init(&m, HBI_QUANT_SCHEME_AFFINE_SYM, 128, 1) == HBI_OK);
    HBI_CHECK(hbi_quant_meta_set_scales(&m, scales, HBI_DTYPE_FP32, 4) == HBI_OK);
    HBI_CHECK(hbi_quant_meta_set_packing(&m, true, HBI_NIBBLE_LOW_FIRST) == HBI_OK);
    HBI_CHECK(hbi_quant_meta_validate(&m, &t) == HBI_OK);
    HBI_CHECK(hbi_tensor_attach_quant(&t, &m) == HBI_OK);
    HBI_CHECK(hbi_tensor_is_quantized(&t));
    HBI_CHECK(hbi_tensor_quant(&t) != NULL);
    HBI_CHECK(hbi_tensor_quant(&t)->scales == scales); /* borrowed, same pointer */

    /* symmetric scheme must not carry zero points. */
    hbi_quant_meta bad;
    hbi_quant_meta_init(&bad, HBI_QUANT_SCHEME_AFFINE_SYM, 128, 1);
    int8_t zp[4] = {0};
    hbi_quant_meta_set_zero_points(&bad, zp, HBI_DTYPE_INT8, 4);
    HBI_CHECK(hbi_quant_meta_validate(&bad, &t) == HBI_ERR_INVALID_ARG);

    /* asymmetric scheme requires zero points. */
    hbi_quant_meta asym;
    hbi_quant_meta_init(&asym, HBI_QUANT_SCHEME_AFFINE_ASYM, 128, 1);
    HBI_CHECK(hbi_quant_meta_validate(&asym, &t) == HBI_ERR_INVALID_ARG);

    /* group_size must divide the grouped axis. */
    hbi_quant_meta g;
    hbi_quant_meta_init(&g, HBI_QUANT_SCHEME_AFFINE_SYM, 100, 1); /* 128 % 100 != 0 */
    hbi_quant_meta_set_scales(&g, scales, HBI_DTYPE_FP32, 4);
    HBI_CHECK(hbi_quant_meta_validate(&g, &t) == HBI_ERR_INVALID_ARG);

    /* a float tensor is not quantizable. */
    hbi_tensor ft;
    hbi_tensor_alloc(&ft, HBI_DTYPE_FP32, &s);
    HBI_CHECK(hbi_quant_meta_validate(&m, &ft) == HBI_ERR_INVALID_ARG);

    hbi_tensor_destroy(&t);
    hbi_tensor_destroy(&ft);
}

/* ── module identity / selftest ─────────────────────────────────────────────── */

static void test_identity(void) {
    HBI_CHECK_STR_EQ(hbi_tensor_name(), "tensor");
    HBI_CHECK(hbi_tensor_selftest() == HBI_OK);
}

int main(void) {
    HBI_TEST_BEGIN("tensor");
    HBI_RUN(test_dtype_tables);
    HBI_RUN(test_packed_nbytes);
    HBI_RUN(test_shape_basic);
    HBI_RUN(test_shape_equal_broadcast);
    HBI_RUN(test_strides_contiguous);
    HBI_RUN(test_alloc_validate_destroy);
    HBI_RUN(test_alloc_aligned);
    HBI_RUN(test_wrap);
    HBI_RUN(test_clone_copy_move);
    HBI_RUN(test_copy_into_strided);
    HBI_RUN(test_reshape);
    HBI_RUN(test_slice_subtensor);
    HBI_RUN(test_transpose_permute);
    HBI_RUN(test_squeeze_unsqueeze);
    HBI_RUN(test_subbyte_view_restrictions);
    HBI_RUN(test_quant_meta);
    HBI_RUN(test_identity);
    return HBI_TEST_END();
}
