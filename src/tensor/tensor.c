/* tensor.c — Tensor runtime data model: dtypes, shapes, strides, tensors,
 * zero-copy views, and quantization metadata.
 *
 * See tensor.h for the full contract. Implementation notes:
 *   - CORRECTNESS-FIRST, no kernels, no inference logic, no GPU code.
 *   - `tensor` is layer 3; the allocator (`memory`, layer 4) is above it, so an
 *     OWNED tensor's backing buffer comes straight from the `platform`
 *     aligned-alloc shim (layer 1) — never from an hbi_allocator. See the
 *     layering note in tensor.h.
 *   - Strides are in ELEMENTS. A byte offset is elem_offset*bits/8; for sub-byte
 *     dtypes that division must be whole, which is why view ops restrict
 *     sub-byte layouts (a stride cannot address a nibble).
 */
#include "tensor/tensor_internal.h"

#include "platform/platform.h"

#include <string.h>

/* ── Overflow-checked signed multiply ─────────────────────────────────────────
 * Returns true and writes *out on success; false on int64 overflow. */
static bool mul_ovf_i64(int64_t a, int64_t b, int64_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }
    int64_t r = a * b;
    if (r / a != b) {
        return false;
    }
    *out = r;
    return true;
}

/* ── Data types ────────────────────────────────────────────────────────────── */

const char *hbi_dtype_str(hbi_dtype dt) {
    switch (dt) {
    case HBI_DTYPE_FP32:
        return "fp32";
    case HBI_DTYPE_FP16:
        return "fp16";
    case HBI_DTYPE_BF16:
        return "bf16";
    case HBI_DTYPE_INT8:
        return "int8";
    case HBI_DTYPE_INT4:
        return "int4";
    case HBI_DTYPE_INT2:
        return "int2";
    case HBI_DTYPE_FP8:
        return "fp8";
    case HBI_DTYPE_NF4:
        return "nf4";
    case HBI_DTYPE_INVALID:
    case HBI_DTYPE_COUNT:
        break;
    }
    return "invalid";
}

uint32_t hbi_dtype_bits(hbi_dtype dt) {
    switch (dt) {
    case HBI_DTYPE_FP32:
        return 32u;
    case HBI_DTYPE_FP16:
    case HBI_DTYPE_BF16:
        return 16u;
    case HBI_DTYPE_INT8:
    case HBI_DTYPE_FP8:
        return 8u;
    case HBI_DTYPE_INT4:
    case HBI_DTYPE_NF4:
        return 4u;
    case HBI_DTYPE_INT2:
        return 2u;
    case HBI_DTYPE_INVALID:
    case HBI_DTYPE_COUNT:
        break;
    }
    return 0u;
}

size_t hbi_dtype_align(hbi_dtype dt) {
    switch (dt) {
    case HBI_DTYPE_FP32:
        return 4u;
    case HBI_DTYPE_FP16:
    case HBI_DTYPE_BF16:
        return 2u;
    case HBI_DTYPE_INT8:
    case HBI_DTYPE_FP8:
        return 1u;
    case HBI_DTYPE_INT4:
    case HBI_DTYPE_INT2:
    case HBI_DTYPE_NF4:
        return 1u; /* sub-byte types report 1 */
    case HBI_DTYPE_INVALID:
    case HBI_DTYPE_COUNT:
        break;
    }
    return 0u;
}

bool hbi_dtype_is_reserved(hbi_dtype dt) {
    return dt == HBI_DTYPE_FP8 || dt == HBI_DTYPE_NF4;
}

bool hbi_dtype_is_valid(hbi_dtype dt) {
    return dt > HBI_DTYPE_INVALID && dt < HBI_DTYPE_COUNT;
}

bool hbi_dtype_is_sub_byte(hbi_dtype dt) {
    return dt == HBI_DTYPE_INT4 || dt == HBI_DTYPE_INT2 || dt == HBI_DTYPE_NF4;
}

hbi_status hbi_dtype_packed_nbytes(hbi_dtype dt, int64_t count, size_t *out) {
    if (out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "packed_nbytes: NULL out");
    }
    if (!hbi_dtype_is_valid(dt) || count < 0) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "packed_nbytes: bad dtype %d or count %lld",
                            (int)dt, (long long)count);
    }
    int64_t bits = (int64_t)hbi_dtype_bits(dt);
    int64_t total_bits = 0;
    if (!mul_ovf_i64(count, bits, &total_bits)) {
        return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "packed_nbytes: bit-count overflow");
    }
    /* (total_bits + 7) / 8, guarding the +7 against overflow. */
    if (total_bits > INT64_MAX - 7) {
        return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "packed_nbytes: byte-count overflow");
    }
    *out = (size_t)((total_bits + 7) / 8);
    return HBI_OK;
}

/* ── Shape ─────────────────────────────────────────────────────────────────── */

hbi_status hbi_shape_init(hbi_shape *out, const int64_t *dims, uint32_t rank) {
    if (out == NULL || (dims == NULL && rank > 0)) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "shape_init: NULL out/dims");
    }
    if (rank > HBI_TENSOR_MAX_RANK) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "shape_init: rank %u > max %u", rank,
                            HBI_TENSOR_MAX_RANK);
    }
    memset(out, 0, sizeof(*out));
    out->rank = rank;
    int64_t acc = 1;
    for (uint32_t i = 0; i < rank; ++i) {
        if (dims[i] <= 0) {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "shape_init: dim[%u]=%lld must be > 0", i,
                                (long long)dims[i]);
        }
        out->dims[i] = dims[i];
        if (!mul_ovf_i64(acc, dims[i], &acc)) {
            return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "shape_init: element-count overflow");
        }
    }
    return HBI_OK;
}

hbi_status hbi_shape_init_scalar(hbi_shape *out) {
    if (out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "shape_init_scalar: NULL out");
    }
    memset(out, 0, sizeof(*out));
    out->rank = 0;
    return HBI_OK;
}

uint32_t hbi_shape_rank(const hbi_shape *s) {
    return s == NULL ? 0u : s->rank;
}

hbi_status hbi_shape_elem_count(const hbi_shape *s, int64_t *out) {
    if (s == NULL || out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "shape_elem_count: NULL arg");
    }
    int64_t acc = 1;
    for (uint32_t i = 0; i < s->rank; ++i) {
        if (!mul_ovf_i64(acc, s->dims[i], &acc)) {
            return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "shape_elem_count: overflow");
        }
    }
    *out = acc; /* rank 0 → 1 */
    return HBI_OK;
}

bool hbi_shape_equal(const hbi_shape *a, const hbi_shape *b) {
    if (a == NULL || b == NULL) {
        return a == b;
    }
    if (a->rank != b->rank) {
        return false;
    }
    for (uint32_t i = 0; i < a->rank; ++i) {
        if (a->dims[i] != b->dims[i]) {
            return false;
        }
    }
    return true;
}

/* Dimension `i` counting from the RIGHT (0 == last axis), defaulting to 1 when
 * the axis lies beyond the shape's rank — the NumPy right-alignment rule. */
static int64_t dim_from_right(const hbi_shape *s, uint32_t i) {
    if (i >= s->rank) {
        return 1;
    }
    return s->dims[s->rank - 1u - i];
}

bool hbi_shape_broadcast_compatible(const hbi_shape *a, const hbi_shape *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    uint32_t n = a->rank > b->rank ? a->rank : b->rank;
    for (uint32_t i = 0; i < n; ++i) {
        int64_t da = dim_from_right(a, i);
        int64_t db = dim_from_right(b, i);
        if (da != db && da != 1 && db != 1) {
            return false;
        }
    }
    return true;
}

hbi_status hbi_shape_broadcast(const hbi_shape *a, const hbi_shape *b, hbi_shape *out) {
    if (a == NULL || b == NULL || out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "shape_broadcast: NULL arg");
    }
    if (!hbi_shape_broadcast_compatible(a, b)) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "shape_broadcast: incompatible shapes");
    }
    uint32_t n = a->rank > b->rank ? a->rank : b->rank;
    hbi_shape r;
    memset(&r, 0, sizeof(r));
    r.rank = n;
    for (uint32_t i = 0; i < n; ++i) {
        int64_t da = dim_from_right(a, i);
        int64_t db = dim_from_right(b, i);
        int64_t d = da > db ? da : db;
        r.dims[n - 1u - i] = d;
    }
    *out = r;
    return HBI_OK;
}

hbi_status hbi_shape_validate(const hbi_shape *s) {
    if (s == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "shape_validate: NULL");
    }
    if (s->rank > HBI_TENSOR_MAX_RANK) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "shape_validate: rank %u > max %u", s->rank,
                            HBI_TENSOR_MAX_RANK);
    }
    int64_t acc = 1;
    for (uint32_t i = 0; i < s->rank; ++i) {
        if (s->dims[i] <= 0) {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "shape_validate: dim[%u]=%lld", i,
                                (long long)s->dims[i]);
        }
        if (!mul_ovf_i64(acc, s->dims[i], &acc)) {
            return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "shape_validate: element-count overflow");
        }
    }
    return HBI_OK;
}

/* ── Strides ───────────────────────────────────────────────────────────────── */

hbi_status hbi_strides_init_contiguous(hbi_strides *out, const hbi_shape *s) {
    if (out == NULL || s == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "strides_init: NULL arg");
    }
    hbi_status st = hbi_shape_validate(s);
    if (st != HBI_OK) {
        return st;
    }
    memset(out, 0, sizeof(*out));
    out->rank = s->rank;
    /* Row-major: stride of the last axis is 1; each earlier axis multiplies. */
    int64_t acc = 1;
    for (uint32_t i = s->rank; i > 0; --i) {
        out->stride[i - 1u] = acc;
        if (!mul_ovf_i64(acc, s->dims[i - 1u], &acc)) {
            return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "strides_init: overflow");
        }
    }
    return HBI_OK;
}

bool hbi_strides_is_contiguous(const hbi_strides *st, const hbi_shape *s) {
    if (st == NULL || s == NULL || st->rank != s->rank) {
        return false;
    }
    int64_t acc = 1;
    for (uint32_t i = s->rank; i > 0; --i) {
        if (st->stride[i - 1u] != acc) {
            return false;
        }
        acc *= s->dims[i - 1u];
    }
    return true;
}

hbi_status hbi_strides_validate(const hbi_strides *st, const hbi_shape *s) {
    if (st == NULL || s == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "strides_validate: NULL arg");
    }
    if (st->rank != s->rank) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "strides_validate: rank %u != shape rank %u",
                            st->rank, s->rank);
    }
    int64_t max_off = 0;
    for (uint32_t i = 0; i < s->rank; ++i) {
        if (st->stride[i] < 0) {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "strides_validate: negative stride[%u]", i);
        }
        int64_t term = 0;
        if (!mul_ovf_i64(s->dims[i] - 1, st->stride[i], &term)) {
            return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "strides_validate: offset overflow");
        }
        if (max_off > INT64_MAX - term) {
            return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "strides_validate: offset overflow");
        }
        max_off += term;
    }
    return HBI_OK;
}

/* ── Internal tensor helpers ─────────────────────────────────────────────────
 * Largest addressable element offset (in elements) for a strided layout, i.e.
 * the span the buffer must cover. Assumes shape+strides already validated. */
static hbi_status span_elems(const hbi_shape *s, const hbi_strides *st, int64_t *out) {
    int64_t max_off = 0;
    for (uint32_t i = 0; i < s->rank; ++i) {
        int64_t term = 0;
        if (!mul_ovf_i64(s->dims[i] - 1, st->stride[i], &term)) {
            return HBI_ERR_SET(HBI_ERR_CORRUPT, 0, "span_elems: overflow");
        }
        max_off += term;
    }
    *out = max_off + 1; /* one past the last addressable element */
    return HBI_OK;
}

/* Recompute the `contiguous` flag and the reachable `nbytes` span for a tensor
 * whose dtype/shape/strides/data are already set. Used by every view op. */
static hbi_status refresh_layout(hbi_tensor *t) {
    t->contiguous = hbi_strides_is_contiguous(&t->strides, &t->shape);
    int64_t span = 0;
    hbi_status st = span_elems(&t->shape, &t->strides, &span);
    if (st != HBI_OK) {
        return st;
    }
    return hbi_dtype_packed_nbytes(t->dtype, span, &t->nbytes);
}

/* Byte offset of the element at multi-index `idx`. Requires the bit offset to be
 * byte-whole (guaranteed for byte dtypes; caller guards sub-byte). */
static int64_t elem_byte_offset(const hbi_strides *st, const int64_t *idx, uint32_t rank,
                                uint32_t bits) {
    int64_t elems = 0;
    for (uint32_t i = 0; i < rank; ++i) {
        elems += idx[i] * st->stride[i];
    }
    return (elems * (int64_t)bits) / 8;
}

/* Generic strided element copy from src→dst over identical shape. Requires a
 * byte-addressable dtype (bits % 8 == 0). Both may be non-contiguous. */
static void strided_copy_bytes(const hbi_tensor *src, hbi_tensor *dst, uint32_t elem_bytes) {
    const hbi_shape *s = &src->shape;
    int64_t idx[HBI_TENSOR_MAX_RANK];
    memset(idx, 0, sizeof(idx));
    int64_t total = 1;
    for (uint32_t i = 0; i < s->rank; ++i) {
        total *= s->dims[i];
    }
    const unsigned char *sb = (const unsigned char *)src->data;
    unsigned char *db = (unsigned char *)dst->data;
    uint32_t bits = elem_bytes * 8u;
    for (int64_t n = 0; n < total; ++n) {
        int64_t so = elem_byte_offset(&src->strides, idx, s->rank, bits);
        int64_t doff = elem_byte_offset(&dst->strides, idx, s->rank, bits);
        memcpy(db + doff, sb + so, elem_bytes);
        /* Odometer increment over the (row-major) index space. */
        for (uint32_t ax = s->rank; ax > 0; --ax) {
            uint32_t a = ax - 1u;
            if (++idx[a] < s->dims[a]) {
                break;
            }
            idx[a] = 0;
        }
    }
}

/* ── Tensor lifecycle ────────────────────────────────────────────────────────
 * Shared body for alloc/alloc_aligned. */
static hbi_status tensor_alloc_impl(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape,
                                    size_t alignment) {
    if (out == NULL || shape == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_alloc: NULL arg");
    }
    if (!hbi_dtype_is_valid(dt)) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_alloc: invalid dtype %d", (int)dt);
    }
    if (hbi_dtype_is_reserved(dt)) {
        return HBI_ERR_SETF(HBI_ERR_UNSUPPORTED, 0, "tensor_alloc: dtype %s not yet allocatable",
                            hbi_dtype_str(dt));
    }
    hbi_status st = hbi_shape_validate(shape);
    if (st != HBI_OK) {
        return st;
    }
    if (!hbi_is_pow2(alignment) || alignment < sizeof(void *) || alignment < hbi_dtype_align(dt)) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_alloc: bad alignment %zu", alignment);
    }

    int64_t count = 0;
    st = hbi_shape_elem_count(shape, &count);
    if (st != HBI_OK) {
        return st;
    }
    size_t bytes = 0;
    st = hbi_dtype_packed_nbytes(dt, count, &bytes);
    if (st != HBI_OK) {
        return st;
    }

    memset(out, 0, sizeof(*out));
    out->dtype = dt;
    out->shape = *shape;
    st = hbi_strides_init_contiguous(&out->strides, shape);
    if (st != HBI_OK) {
        return st;
    }

    /* An empty logical size (impossible here, dims > 0) would still allocate at
     * least the packed count; guard the 0-byte edge for robustness. */
    void *buf = hbi_aligned_alloc(alignment, bytes == 0 ? alignment : bytes);
    if (buf == NULL) {
        return HBI_ERR_SETF(HBI_ERR_OOM, hbi_os_errno(), "tensor_alloc: %zu bytes failed", bytes);
    }
    memset(buf, 0, bytes);

    out->data = buf;
    out->nbytes = bytes;
    out->align = alignment;
    out->ownership = HBI_TENSOR_OWNED;
    out->device = HBI_TENSOR_DEVICE_CPU;
    out->read_only = false;
    out->contiguous = true;
    /* quant already zeroed → scheme == HBI_QUANT_SCHEME_NONE. */
    return HBI_OK;
}

hbi_status hbi_tensor_alloc(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape) {
    return tensor_alloc_impl(out, dt, shape, HBI_TENSOR_DEFAULT_ALIGN);
}

hbi_status hbi_tensor_alloc_aligned(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape,
                                    size_t alignment) {
    return tensor_alloc_impl(out, dt, shape, alignment);
}

/* Shared body for wrap/wrap_readonly. */
static hbi_status tensor_wrap_impl(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape,
                                   void *data, size_t nbytes, bool read_only) {
    if (out == NULL || shape == NULL || data == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_wrap: NULL arg");
    }
    if (!hbi_dtype_is_valid(dt) || hbi_dtype_is_reserved(dt)) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_wrap: bad dtype %d", (int)dt);
    }
    hbi_status st = hbi_shape_validate(shape);
    if (st != HBI_OK) {
        return st;
    }
    size_t align = hbi_dtype_align(dt);
    if (align != 0 && ((uintptr_t)data % align) != 0) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_wrap: data not %zu-aligned", align);
    }
    int64_t count = 0;
    st = hbi_shape_elem_count(shape, &count);
    if (st != HBI_OK) {
        return st;
    }
    size_t need = 0;
    st = hbi_dtype_packed_nbytes(dt, count, &need);
    if (st != HBI_OK) {
        return st;
    }
    if (nbytes < need) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_wrap: nbytes %zu < needed %zu", nbytes,
                            need);
    }

    memset(out, 0, sizeof(*out));
    out->dtype = dt;
    out->shape = *shape;
    st = hbi_strides_init_contiguous(&out->strides, shape);
    if (st != HBI_OK) {
        return st;
    }
    out->data = data;
    out->nbytes = nbytes;
    out->align = align == 0 ? 1u : align;
    out->ownership = HBI_TENSOR_BORROWED;
    out->device = HBI_TENSOR_DEVICE_CPU;
    out->read_only = read_only;
    out->contiguous = true;
    return HBI_OK;
}

hbi_status hbi_tensor_wrap(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape, void *data,
                           size_t nbytes) {
    return tensor_wrap_impl(out, dt, shape, data, nbytes, false);
}

hbi_status hbi_tensor_wrap_readonly(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape,
                                    const void *data, size_t nbytes) {
    /* Cast away const only to store the pointer; read_only guards mutation. */
    return tensor_wrap_impl(out, dt, shape, (void *)(uintptr_t)data, nbytes, true);
}

void hbi_tensor_destroy(hbi_tensor *t) {
    if (t == NULL) {
        return;
    }
    if (t->ownership == HBI_TENSOR_OWNED && t->data != NULL) {
        hbi_aligned_free(t->data);
    }
    memset(t, 0, sizeof(*t));
}

hbi_status hbi_tensor_clone(const hbi_tensor *src, hbi_tensor *out) {
    if (src == NULL || out == NULL || src->data == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_clone: NULL arg");
    }
    hbi_status st = hbi_tensor_alloc(out, src->dtype, &src->shape);
    if (st != HBI_OK) {
        return st;
    }
    if (src->contiguous) {
        memcpy(out->data, src->data, out->nbytes);
    } else if (hbi_dtype_is_sub_byte(src->dtype)) {
        hbi_tensor_destroy(out);
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "tensor_clone: non-contiguous sub-byte source");
    } else {
        strided_copy_bytes(src, out, hbi_dtype_bits(src->dtype) / 8u);
    }
    /* Copy quant metadata by value; inner pointers stay borrowed (same as src). */
    out->quant = src->quant;
    return HBI_OK;
}

hbi_status hbi_tensor_copy_into(const hbi_tensor *src, hbi_tensor *dst) {
    if (src == NULL || dst == NULL || src->data == NULL || dst->data == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_copy_into: NULL arg");
    }
    if (dst->read_only) {
        return HBI_ERR_SET(HBI_ERR_STATE, 0, "tensor_copy_into: dst is read-only");
    }
    if (src->dtype != dst->dtype || !hbi_shape_equal(&src->shape, &dst->shape)) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_copy_into: dtype/shape mismatch");
    }
    if (src->contiguous && dst->contiguous) {
        int64_t count = 0;
        hbi_status st = hbi_shape_elem_count(&src->shape, &count);
        if (st != HBI_OK) {
            return st;
        }
        size_t bytes = 0;
        st = hbi_dtype_packed_nbytes(src->dtype, count, &bytes);
        if (st != HBI_OK) {
            return st;
        }
        if (dst->nbytes < bytes) {
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_copy_into: dst capacity too small");
        }
        memcpy(dst->data, src->data, bytes);
        return HBI_OK;
    }
    if (hbi_dtype_is_sub_byte(src->dtype)) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0,
                           "tensor_copy_into: non-contiguous sub-byte copy");
    }
    strided_copy_bytes(src, dst, hbi_dtype_bits(src->dtype) / 8u);
    return HBI_OK;
}

hbi_status hbi_tensor_move(hbi_tensor *dst, hbi_tensor *src) {
    if (dst == NULL || src == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_move: NULL arg");
    }
    *dst = *src;
    memset(src, 0, sizeof(*src));
    return HBI_OK;
}

/* ── Accessors ─────────────────────────────────────────────────────────────── */

hbi_dtype hbi_tensor_dtype(const hbi_tensor *t) {
    return t == NULL ? HBI_DTYPE_INVALID : t->dtype;
}

const hbi_shape *hbi_tensor_shape(const hbi_tensor *t) {
    return t == NULL ? NULL : &t->shape;
}

const hbi_strides *hbi_tensor_strides(const hbi_tensor *t) {
    return t == NULL ? NULL : &t->strides;
}

size_t hbi_tensor_nbytes(const hbi_tensor *t) {
    return t == NULL ? 0u : t->nbytes;
}

const void *hbi_tensor_cdata(const hbi_tensor *t) {
    return t == NULL ? NULL : t->data;
}

void *hbi_tensor_data_mut(hbi_tensor *t) {
    if (t == NULL || t->read_only) {
        return NULL;
    }
    return t->data;
}

bool hbi_tensor_is_contiguous(const hbi_tensor *t) {
    return t != NULL && t->contiguous;
}

bool hbi_tensor_is_readonly(const hbi_tensor *t) {
    return t != NULL && t->read_only;
}

bool hbi_tensor_owns_data(const hbi_tensor *t) {
    /* Requires live data: HBI_TENSOR_OWNED is 0, so a zeroed (moved-from or
     * destroyed) tensor must not masquerade as owning its (NULL) buffer. */
    return t != NULL && t->ownership == HBI_TENSOR_OWNED && t->data != NULL;
}

hbi_tensor_device hbi_tensor_device_of(const hbi_tensor *t) {
    return t == NULL ? HBI_TENSOR_DEVICE_CPU : t->device;
}

/* ── Views ─────────────────────────────────────────────────────────────────── */

/* Seed *out from parent as a VIEW: copies metadata, marks ownership VIEW,
 * inherits read_only/device/quant, drops nothing. Caller then adjusts
 * shape/strides/data and calls refresh_layout. */
static void view_seed(const hbi_tensor *parent, hbi_tensor *out) {
    *out = *parent;
    out->ownership = HBI_TENSOR_VIEW;
}

hbi_status hbi_tensor_view(const hbi_tensor *parent, hbi_tensor *out) {
    if (parent == NULL || out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_view: NULL arg");
    }
    view_seed(parent, out);
    return HBI_OK;
}

hbi_status hbi_tensor_reshape(const hbi_tensor *parent, const hbi_shape *new_shape,
                              hbi_tensor *out) {
    if (parent == NULL || new_shape == NULL || out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_reshape: NULL arg");
    }
    if (!parent->contiguous) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "tensor_reshape: parent not contiguous");
    }
    hbi_status st = hbi_shape_validate(new_shape);
    if (st != HBI_OK) {
        return st;
    }
    int64_t old_n = 0, new_n = 0;
    if ((st = hbi_shape_elem_count(&parent->shape, &old_n)) != HBI_OK) {
        return st;
    }
    if ((st = hbi_shape_elem_count(new_shape, &new_n)) != HBI_OK) {
        return st;
    }
    if (old_n != new_n) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_reshape: %lld != %lld elements",
                            (long long)old_n, (long long)new_n);
    }
    view_seed(parent, out);
    out->shape = *new_shape;
    st = hbi_strides_init_contiguous(&out->strides, new_shape);
    if (st != HBI_OK) {
        return st;
    }
    return refresh_layout(out);
}

hbi_status hbi_tensor_slice(const hbi_tensor *parent, uint32_t axis, int64_t start, int64_t count,
                            hbi_tensor *out) {
    if (parent == NULL || out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_slice: NULL arg");
    }
    if (axis >= parent->shape.rank) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_slice: axis %u >= rank %u", axis,
                            parent->shape.rank);
    }
    int64_t dim = parent->shape.dims[axis];
    if (start < 0 || count <= 0 || start + count > dim) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_slice: [%lld,%lld) out of [0,%lld)",
                            (long long)start, (long long)(start + count), (long long)dim);
    }
    int64_t elem_off = start * parent->strides.stride[axis];
    if (hbi_dtype_is_sub_byte(parent->dtype)) {
        int64_t bit_off = elem_off * (int64_t)hbi_dtype_bits(parent->dtype);
        if (bit_off % 8 != 0) {
            return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0,
                               "tensor_slice: sub-byte start not byte-aligned");
        }
    }
    view_seed(parent, out);
    out->shape.dims[axis] = count;
    uint32_t bits = hbi_dtype_bits(parent->dtype);
    out->data = (unsigned char *)parent->data + (elem_off * (int64_t)bits) / 8;
    out->align = hbi_dtype_align(parent->dtype);
    if (out->align == 0) {
        out->align = 1u;
    }
    return refresh_layout(out);
}

hbi_status hbi_tensor_subtensor(const hbi_tensor *parent, const int64_t *starts,
                                const int64_t *counts, uint32_t n, hbi_tensor *out) {
    if (parent == NULL || out == NULL || starts == NULL || counts == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_subtensor: NULL arg");
    }
    if (n > parent->shape.rank) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_subtensor: n %u > rank %u", n,
                            parent->shape.rank);
    }
    int64_t elem_off = 0;
    for (uint32_t i = 0; i < n; ++i) {
        int64_t dim = parent->shape.dims[i];
        if (starts[i] < 0 || counts[i] <= 0 || starts[i] + counts[i] > dim) {
            return HBI_ERR_SETF(
                HBI_ERR_INVALID_ARG, 0, "tensor_subtensor: axis %u [%lld,%lld) out of [0,%lld)", i,
                (long long)starts[i], (long long)(starts[i] + counts[i]), (long long)dim);
        }
        elem_off += starts[i] * parent->strides.stride[i];
    }
    if (hbi_dtype_is_sub_byte(parent->dtype)) {
        int64_t bit_off = elem_off * (int64_t)hbi_dtype_bits(parent->dtype);
        if (bit_off % 8 != 0) {
            return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0,
                               "tensor_subtensor: sub-byte offset not byte-aligned");
        }
    }
    view_seed(parent, out);
    for (uint32_t i = 0; i < n; ++i) {
        out->shape.dims[i] = counts[i];
    }
    uint32_t bits = hbi_dtype_bits(parent->dtype);
    out->data = (unsigned char *)parent->data + (elem_off * (int64_t)bits) / 8;
    out->align = hbi_dtype_align(parent->dtype);
    if (out->align == 0) {
        out->align = 1u;
    }
    return refresh_layout(out);
}

hbi_status hbi_tensor_transpose(const hbi_tensor *parent, uint32_t axis_a, uint32_t axis_b,
                                hbi_tensor *out) {
    if (parent == NULL || out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_transpose: NULL arg");
    }
    if (axis_a >= parent->shape.rank || axis_b >= parent->shape.rank) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_transpose: axis out of range");
    }
    if (hbi_dtype_is_sub_byte(parent->dtype) && axis_a != axis_b) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "tensor_transpose: sub-byte dtype");
    }
    view_seed(parent, out);
    int64_t td = out->shape.dims[axis_a];
    out->shape.dims[axis_a] = out->shape.dims[axis_b];
    out->shape.dims[axis_b] = td;
    int64_t ts = out->strides.stride[axis_a];
    out->strides.stride[axis_a] = out->strides.stride[axis_b];
    out->strides.stride[axis_b] = ts;
    return refresh_layout(out);
}

hbi_status hbi_tensor_permute(const hbi_tensor *parent, const uint32_t *perm, uint32_t n,
                              hbi_tensor *out) {
    if (parent == NULL || out == NULL || perm == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_permute: NULL arg");
    }
    if (n != parent->shape.rank) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_permute: n %u != rank %u", n,
                            parent->shape.rank);
    }
    /* Validate `perm` is a bijection over [0,n). */
    bool seen[HBI_TENSOR_MAX_RANK] = {false};
    bool identity = true;
    for (uint32_t i = 0; i < n; ++i) {
        if (perm[i] >= n || seen[perm[i]]) {
            return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_permute: perm not a bijection");
        }
        seen[perm[i]] = true;
        if (perm[i] != i) {
            identity = false;
        }
    }
    if (hbi_dtype_is_sub_byte(parent->dtype) && !identity) {
        return HBI_ERR_SET(HBI_ERR_UNSUPPORTED, 0, "tensor_permute: sub-byte non-identity");
    }
    view_seed(parent, out);
    for (uint32_t i = 0; i < n; ++i) {
        out->shape.dims[i] = parent->shape.dims[perm[i]];
        out->strides.stride[i] = parent->strides.stride[perm[i]];
    }
    return refresh_layout(out);
}

hbi_status hbi_tensor_squeeze(const hbi_tensor *parent, hbi_tensor *out) {
    if (parent == NULL || out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_squeeze: NULL arg");
    }
    view_seed(parent, out);
    uint32_t w = 0;
    for (uint32_t i = 0; i < parent->shape.rank; ++i) {
        if (parent->shape.dims[i] != 1) {
            out->shape.dims[w] = parent->shape.dims[i];
            out->strides.stride[w] = parent->strides.stride[i];
            ++w;
        }
    }
    for (uint32_t i = w; i < HBI_TENSOR_MAX_RANK; ++i) {
        out->shape.dims[i] = 0;
        out->strides.stride[i] = 0;
    }
    out->shape.rank = w;
    out->strides.rank = w;
    return refresh_layout(out);
}

hbi_status hbi_tensor_unsqueeze(const hbi_tensor *parent, uint32_t axis, hbi_tensor *out) {
    if (parent == NULL || out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_unsqueeze: NULL arg");
    }
    if (axis > parent->shape.rank) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_unsqueeze: axis %u > rank %u", axis,
                            parent->shape.rank);
    }
    if (parent->shape.rank + 1u > HBI_TENSOR_MAX_RANK) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_unsqueeze: rank would exceed max");
    }
    view_seed(parent, out);
    uint32_t nr = parent->shape.rank + 1u;
    /* The inserted axis has size 1; its stride is that of the following axis
     * (or 1 at the tail) so the layout stays consistent. */
    for (uint32_t i = 0; i < nr; ++i) {
        if (i < axis) {
            out->shape.dims[i] = parent->shape.dims[i];
            out->strides.stride[i] = parent->strides.stride[i];
        } else if (i == axis) {
            out->shape.dims[i] = 1;
            int64_t s = (axis < parent->shape.rank) ? parent->strides.stride[axis] : 1;
            out->strides.stride[i] = s;
        } else {
            out->shape.dims[i] = parent->shape.dims[i - 1u];
            out->strides.stride[i] = parent->strides.stride[i - 1u];
        }
    }
    out->shape.rank = nr;
    out->strides.rank = nr;
    return refresh_layout(out);
}

/* ── Quantization metadata ───────────────────────────────────────────────────── */

hbi_status hbi_quant_meta_init(hbi_quant_meta *out, hbi_quant_scheme scheme, int32_t group_size,
                               uint32_t group_axis) {
    if (out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "quant_meta_init: NULL out");
    }
    memset(out, 0, sizeof(*out));
    out->scheme = scheme;
    out->group_size = group_size;
    out->group_axis = group_axis;
    out->scale_dtype = HBI_DTYPE_FP32;
    out->zp_dtype = HBI_DTYPE_INT8;
    out->nibble_order = HBI_NIBBLE_LOW_FIRST;
    return HBI_OK;
}

hbi_status hbi_quant_meta_set_scales(hbi_quant_meta *m, const void *scales, hbi_dtype dt,
                                     size_t n) {
    if (m == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "quant_meta_set_scales: NULL meta");
    }
    m->scales = scales;
    m->scale_dtype = dt;
    m->num_scales = n;
    return HBI_OK;
}

hbi_status hbi_quant_meta_set_zero_points(hbi_quant_meta *m, const void *zp, hbi_dtype dt,
                                          size_t n) {
    if (m == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "quant_meta_set_zero_points: NULL meta");
    }
    m->zero_points = zp;
    m->zp_dtype = dt;
    m->num_zero_points = n;
    return HBI_OK;
}

hbi_status hbi_quant_meta_set_packing(hbi_quant_meta *m, bool offset_binary,
                                      hbi_nibble_order order) {
    if (m == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "quant_meta_set_packing: NULL meta");
    }
    m->offset_binary = offset_binary;
    m->nibble_order = order;
    return HBI_OK;
}

hbi_status hbi_quant_meta_validate(const hbi_quant_meta *m, const hbi_tensor *t) {
    if (m == NULL || t == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "quant_meta_validate: NULL arg");
    }
    if (m->scheme == HBI_QUANT_SCHEME_NONE) {
        return HBI_OK; /* nothing to validate */
    }
    /* Only integer/sub-byte tensors are quantizable. */
    switch (t->dtype) {
    case HBI_DTYPE_INT8:
    case HBI_DTYPE_INT4:
    case HBI_DTYPE_INT2:
    case HBI_DTYPE_NF4:
        break;
    default:
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "quant_meta_validate: dtype %s not quantizable",
                            hbi_dtype_str(t->dtype));
    }
    if (m->group_axis >= t->shape.rank && t->shape.rank != 0) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "quant_meta_validate: group_axis %u >= rank %u",
                            m->group_axis, t->shape.rank);
    }
    if (m->group_size > 0 && t->shape.rank != 0) {
        int64_t axis_dim = t->shape.dims[m->group_axis];
        if (axis_dim % m->group_size != 0) {
            return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0,
                                "quant_meta_validate: group_size %d does not divide dim %lld",
                                m->group_size, (long long)axis_dim);
        }
    }
    /* Asymmetric schemes need zero points; symmetric must not carry them. */
    if (m->scheme == HBI_QUANT_SCHEME_AFFINE_ASYM && m->zero_points == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "quant_meta_validate: asym scheme needs zp");
    }
    if (m->scheme == HBI_QUANT_SCHEME_AFFINE_SYM && m->zero_points != NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "quant_meta_validate: sym scheme carries zp");
    }
    return HBI_OK;
}

hbi_status hbi_tensor_attach_quant(hbi_tensor *t, const hbi_quant_meta *m) {
    if (t == NULL || m == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_attach_quant: NULL arg");
    }
    hbi_status st = hbi_quant_meta_validate(m, t);
    if (st != HBI_OK) {
        return st;
    }
    t->quant = *m; /* inner scales/zero_points stay borrowed */
    return HBI_OK;
}

const hbi_quant_meta *hbi_tensor_quant(const hbi_tensor *t) {
    if (t == NULL || t->quant.scheme == HBI_QUANT_SCHEME_NONE) {
        return NULL;
    }
    return &t->quant;
}

bool hbi_tensor_is_quantized(const hbi_tensor *t) {
    return t != NULL && t->quant.scheme != HBI_QUANT_SCHEME_NONE;
}

/* ── Validation ────────────────────────────────────────────────────────────── */

bool hbi_tensor_is_aligned(const hbi_tensor *t, size_t alignment) {
    if (t == NULL || !hbi_is_pow2(alignment)) {
        return false;
    }
    return ((uintptr_t)t->data % alignment) == 0;
}

hbi_status hbi_tensor_validate(const hbi_tensor *t) {
    if (t == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_validate: NULL");
    }
    if (!hbi_dtype_is_valid(t->dtype)) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "tensor_validate: bad dtype %d", (int)t->dtype);
    }
    hbi_status st = hbi_shape_validate(&t->shape);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_strides_validate(&t->strides, &t->shape);
    if (st != HBI_OK) {
        return st;
    }
    if (t->data == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "tensor_validate: NULL data");
    }
    /* Capacity must cover the reachable span. */
    int64_t span = 0;
    st = span_elems(&t->shape, &t->strides, &span);
    if (st != HBI_OK) {
        return st;
    }
    size_t need = 0;
    st = hbi_dtype_packed_nbytes(t->dtype, span, &need);
    if (st != HBI_OK) {
        return st;
    }
    if (t->nbytes < need) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "tensor_validate: nbytes %zu < needed %zu",
                            t->nbytes, need);
    }
    if (t->align != 0 && !hbi_tensor_is_aligned(t, t->align)) {
        return HBI_ERR_SETF(HBI_ERR_CORRUPT, 0, "tensor_validate: data not %zu-aligned", t->align);
    }
    if (t->contiguous != hbi_strides_is_contiguous(&t->strides, &t->shape)) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "tensor_validate: contiguous flag stale");
    }
    return hbi_quant_meta_validate(&t->quant, t);
}

/* ── Module identity / self-test ─────────────────────────────────────────────
 * A quick internal invariant sweep so the CTest scaffold and higher layers can
 * assert the module is well-formed without a full unit-test run. */
const char *hbi_tensor_name(void) {
    return "tensor";
}

hbi_status hbi_tensor_selftest(void) {
    /* dtype tables agree with themselves. */
    if (hbi_dtype_bits(HBI_DTYPE_FP32) != 32u || hbi_dtype_bits(HBI_DTYPE_INT4) != 4u) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "tensor_selftest: dtype bits table wrong");
    }
    if (!hbi_dtype_is_sub_byte(HBI_DTYPE_INT4) || hbi_dtype_is_sub_byte(HBI_DTYPE_INT8)) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "tensor_selftest: sub_byte classification wrong");
    }

    /* A small contiguous tensor round-trips alloc → validate → destroy. */
    int64_t dims[2] = {2, 3};
    hbi_shape s;
    hbi_status st = hbi_shape_init(&s, dims, 2);
    if (st != HBI_OK) {
        return st;
    }
    int64_t n = 0;
    if (hbi_shape_elem_count(&s, &n) != HBI_OK || n != 6) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "tensor_selftest: elem_count wrong");
    }
    hbi_tensor t;
    st = hbi_tensor_alloc(&t, HBI_DTYPE_FP32, &s);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_tensor_validate(&t);
    if (st != HBI_OK) {
        hbi_tensor_destroy(&t);
        return st;
    }
    /* A transpose view must be non-contiguous but still valid. */
    hbi_tensor tv;
    st = hbi_tensor_transpose(&t, 0, 1, &tv);
    if (st != HBI_OK) {
        hbi_tensor_destroy(&t);
        return st;
    }
    if (hbi_tensor_is_contiguous(&tv) || hbi_tensor_validate(&tv) != HBI_OK) {
        hbi_tensor_destroy(&t);
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "tensor_selftest: transpose view invalid");
    }
    hbi_tensor_destroy(&t); /* view holds no buffer of its own */
    return HBI_OK;
}
