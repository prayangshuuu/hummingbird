/* tensor.h — Tensor runtime: dtypes, shapes, strides, tensors, zero-copy views,
 * and quantization metadata. This is the foundation every computation in the
 * engine is expressed over (§3.6). It is CORRECTNESS-FIRST and deliberately
 * contains NO inference logic, NO model-specific assumptions, NO GPU code, and
 * NO quantization kernels — only the data model those layers build on.
 *
 * Core-public header for the `tensor` module (layer 3). Symbols are prefixed
 * `hbi_` (internal, no stability guarantee); external embedders use
 * <hummingbird/hummingbird.h>. See docs/architecture/03-dependency-graph.md.
 *
 * ── Layering note (why there is no allocator parameter) ──────────────────────
 * `tensor` is layer 3; the `memory` module's allocator (`hbi_allocator`) is
 * layer 4. A layer-3 module may not depend upward, so tensors NEVER take an
 * allocator. Owned tensors allocate their backing buffer through the `platform`
 * aligned-alloc shim directly (like `common`/`platform` do); every other tensor
 * borrows a caller-provided buffer and never frees it. This keeps the graph
 * acyclic and is a deliberate constraint, not an oversight.
 *
 * ── Ownership model ──────────────────────────────────────────────────────────
 *   OWNED    — the module allocated the buffer; hbi_tensor_destroy frees it.
 *   BORROWED — wraps a caller buffer; destroy never frees it.
 *   VIEW     — aliases another tensor's buffer (slice/reshape/transpose/…);
 *              never frees. A view must NOT outlive its parent's buffer — no
 *              refcount or parent pointer is stored; lifetime is the caller's
 *              contract.
 *   MMAP     — RESERVED for a later memory-mapped path; not usable yet.
 *
 * ── Thread-safety ────────────────────────────────────────────────────────────
 * An hbi_tensor is plain data with no internal locking. Concurrent mutation of
 * the same tensor is the caller's responsibility. Distinct tensors (including a
 * view and its parent, if only read) are independent. The error record used on
 * failures is thread-local (common.h), so diagnostics are per-thread.
 */
#ifndef HB_TENSOR_H
#define HB_TENSOR_H

#include "common/common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum tensor rank. Rank 0 is a scalar (element count 1). Fixed so shapes
 * and strides are POD value types with no heap indirection. */
#define HBI_TENSOR_MAX_RANK 8u

/* Default backing-buffer alignment for owned tensors (AVX-512 register / common
 * cache line). A power of two and a multiple of sizeof(void*), as the platform
 * aligned-alloc shim requires. */
#define HBI_TENSOR_DEFAULT_ALIGN 64u

/* ── Data types ──────────────────────────────────────────────────────────────
 * The element formats the runtime can describe. FP8/NF4 are RESERVED: they are
 * query-able (bits/str) but allocation of them returns HBI_ERR_UNSUPPORTED until
 * a later phase. INT4/INT2 are sub-byte and packed. The tensor runtime assumes
 * no model architecture — these are pure numeric formats. */
typedef enum hbi_dtype {
    HBI_DTYPE_INVALID = 0, /* zero-init guard; never a usable type */
    HBI_DTYPE_FP32,
    HBI_DTYPE_FP16,
    HBI_DTYPE_BF16,
    HBI_DTYPE_INT8,
    HBI_DTYPE_INT4, /* sub-byte, 4 bits, packed 2/byte */
    HBI_DTYPE_INT2, /* sub-byte, 2 bits, packed 4/byte */
    HBI_DTYPE_FP8,  /* RESERVED — query-able, not yet allocatable */
    HBI_DTYPE_NF4,  /* RESERVED — query-able, not yet allocatable */
    HBI_DTYPE_COUNT /* sentinel: number of dtypes (not a dtype) */
} hbi_dtype;

/* Stable lower-case spelling ("fp32","int4",…). Never NULL; "invalid" for out-
 * of-range. Safe on any thread, cannot fail. */
const char *hbi_dtype_str(hbi_dtype dt);

/* Bits per element (fp32=32, int8=8, int4=4, int2=2). 0 for INVALID/COUNT.
 * Cannot fail. */
uint32_t hbi_dtype_bits(hbi_dtype dt);

/* Natural byte alignment for one element (fp32=4, fp16/bf16=2, int8=1; sub-byte
 * types report 1). 0 for invalid. Cannot fail. */
size_t hbi_dtype_align(hbi_dtype dt);

/* True for a real, usable element type (excludes INVALID, COUNT, and — for
 * allocation — the reserved types; use hbi_dtype_is_reserved to distinguish). */
bool hbi_dtype_is_valid(hbi_dtype dt);

/* True for FP8/NF4: describable but not yet backed by allocation/kernels. */
bool hbi_dtype_is_reserved(hbi_dtype dt);

/* True for sub-byte packed types (INT4, INT2, NF4). */
bool hbi_dtype_is_sub_byte(hbi_dtype dt);

/* Packed byte count for `count` elements of `dt`, i.e. (count*bits + 7)/8.
 * Correctly handles sub-byte packing (never truncates bits/8 to 0). Writes
 * *out on success. Fails HBI_ERR_INVALID_ARG (bad dtype/NULL/negative count) or
 * HBI_ERR_CORRUPT on multiply overflow. */
hbi_status hbi_dtype_packed_nbytes(hbi_dtype dt, int64_t count, size_t *out);

/* ── Device tag ──────────────────────────────────────────────────────────────
 * A forward-looking placement tag. CPU is the only functional value today; the
 * reserved value carries no semantics yet. This is a local tag, NOT a handle to
 * the `device` module (which is a lateral layer-3 dependency and thus
 * forbidden). */
typedef enum hbi_tensor_device {
    HBI_TENSOR_DEVICE_CPU = 0,
    HBI_TENSOR_DEVICE_RESERVED_1 /* future accelerator; no behavior now */
} hbi_tensor_device;

/* ── Buffer ownership kind ─────────────────────────────────────────────────── */
typedef enum hbi_tensor_ownership {
    HBI_TENSOR_OWNED = 0, /* module allocated; destroy frees */
    HBI_TENSOR_BORROWED,  /* caller buffer; destroy never frees */
    HBI_TENSOR_VIEW,      /* aliases another tensor; never frees */
    HBI_TENSOR_MMAP       /* RESERVED memory-mapped; not usable yet */
} hbi_tensor_ownership;

/* ── Shape ───────────────────────────────────────────────────────────────────
 * A copyable value type: rank plus positive dimensions. Unused dims are 0. */
typedef struct hbi_shape {
    uint32_t rank;                     /* 0..HBI_TENSOR_MAX_RANK (0 == scalar) */
    int64_t dims[HBI_TENSOR_MAX_RANK]; /* each > 0 for used axes; else 0 */
} hbi_shape;

/* Initialize *out from `dims[0..rank)`. Every dim must be > 0 and the product
 * must not overflow int64_t. Fails HBI_ERR_INVALID_ARG (NULL, rank too large,
 * non-positive dim) or HBI_ERR_CORRUPT (element-count overflow). */
hbi_status hbi_shape_init(hbi_shape *out, const int64_t *dims, uint32_t rank);

/* Initialize *out as a scalar (rank 0, element count 1). NULL out fails. */
hbi_status hbi_shape_init_scalar(hbi_shape *out);

/* Rank accessor (0 if s is NULL). */
uint32_t hbi_shape_rank(const hbi_shape *s);

/* Total element count (scalar → 1). Fails HBI_ERR_INVALID_ARG (NULL) or
 * HBI_ERR_CORRUPT (overflow). */
hbi_status hbi_shape_elem_count(const hbi_shape *s, int64_t *out);

/* Exact structural equality (rank and every dim). NULLs compare unequal unless
 * both NULL (→ true). */
bool hbi_shape_equal(const hbi_shape *a, const hbi_shape *b);

/* NumPy-style right-aligned broadcast compatibility (dims equal, or one is 1;
 * scalars broadcast to anything). */
bool hbi_shape_broadcast_compatible(const hbi_shape *a, const hbi_shape *b);

/* Compute the broadcast result shape of a and b into *out. Fails
 * HBI_ERR_INVALID_ARG when incompatible or on NULL. */
hbi_status hbi_shape_broadcast(const hbi_shape *a, const hbi_shape *b, hbi_shape *out);

/* Validate rank/dim bounds and multiply-overflow invariants. */
hbi_status hbi_shape_validate(const hbi_shape *s);

/* ── Strides (in ELEMENTS, not bytes) ────────────────────────────────────────
 * Element-domain strides pair with a shape of equal rank. Signed to reserve
 * negative (reversed) strides for a future flip-view; current validation
 * requires each stride >= 0. */
typedef struct hbi_strides {
    uint32_t rank;
    int64_t stride[HBI_TENSOR_MAX_RANK];
} hbi_strides;

/* Fill *out with C-contiguous (row-major) strides for `s`. NULL fails. */
hbi_status hbi_strides_init_contiguous(hbi_strides *out, const hbi_shape *s);

/* True iff `st` equals the C-contiguous strides for `s`. */
bool hbi_strides_is_contiguous(const hbi_strides *st, const hbi_shape *s);

/* Validate: rank matches `s`, strides non-negative, and max addressed element
 * offset does not overflow int64_t. */
hbi_status hbi_strides_validate(const hbi_strides *st, const hbi_shape *s);

/* ── Quantization metadata (storage only — NO kernels) ───────────────────────
 * Purely descriptive. `scales` and `zero_points` are BORROWED pointers: the
 * tensor never allocates, copies, or frees them — the caller owns them and they
 * must outlive the tensor (this holds through hbi_tensor_clone too). */
typedef enum hbi_quant_scheme {
    HBI_QUANT_SCHEME_NONE = 0,    /* not quantized */
    HBI_QUANT_SCHEME_AFFINE_ASYM, /* scale + zero-point */
    HBI_QUANT_SCHEME_AFFINE_SYM,  /* scale only */
    HBI_QUANT_SCHEME_RESERVED_1   /* future (GPTQ/AWQ/NF4-style) */
} hbi_quant_scheme;

typedef enum hbi_nibble_order {
    HBI_NIBBLE_LOW_FIRST = 0, /* element k in low nibble of byte k/2 */
    HBI_NIBBLE_HIGH_FIRST
} hbi_nibble_order;

typedef struct hbi_quant_meta {
    hbi_quant_scheme scheme; /* NONE == absent */
    int32_t group_size;      /* elements per quant group; <=0 == per-tensor */
    uint32_t group_axis;     /* axis the grouping runs along */
    const void *scales;      /* BORROWED; may be NULL until set */
    hbi_dtype scale_dtype;   /* dtype of scale elements */
    size_t num_scales;
    const void *zero_points; /* BORROWED; NULL for symmetric schemes */
    hbi_dtype zp_dtype;
    size_t num_zero_points;
    bool offset_binary; /* sub-byte value encoding uses offset binary */
    hbi_nibble_order nibble_order;
} hbi_quant_meta;

/* ── Tensor ──────────────────────────────────────────────────────────────────
 * A transparent value struct: element type, shape, element strides, a data
 * pointer, and the ownership/flags that govern its lifetime. Transparent (not
 * opaque) so views are stack-allocatable and lower layers can read metadata
 * without accessor churn — but prefer the accessors below over reaching in. */
typedef struct hbi_tensor {
    hbi_dtype dtype;
    hbi_shape shape;
    hbi_strides strides; /* element strides */
    void *data;          /* start of this tensor's logical data */
    size_t nbytes;       /* bytes reachable from `data` */
    size_t align;        /* alignment `data` is guaranteed to */
    hbi_tensor_ownership ownership;
    hbi_tensor_device device;
    bool read_only;
    bool contiguous;      /* cached; recomputed by view ops */
    hbi_quant_meta quant; /* scheme==NONE when not quantized */
} hbi_tensor;

/* ── Tensor lifecycle ────────────────────────────────────────────────────────
 * alloc/alloc_aligned produce OWNED tensors (buffer from the platform aligned-
 * alloc shim), zero-initialized. wrap/wrap_readonly produce BORROWED tensors
 * over a caller buffer. destroy frees only OWNED buffers and always zeroes the
 * struct (idempotent, NULL-safe). */

/* Allocate an OWNED, C-contiguous tensor at HBI_TENSOR_DEFAULT_ALIGN. Fails
 * HBI_ERR_INVALID_ARG (NULL/invalid shape), HBI_ERR_UNSUPPORTED (reserved
 * dtype), HBI_ERR_CORRUPT (size overflow), or HBI_ERR_OOM. */
hbi_status hbi_tensor_alloc(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape);

/* As hbi_tensor_alloc with an explicit `alignment` (power of two, >= the dtype's
 * natural alignment, >= sizeof(void*)). Bad alignment fails HBI_ERR_INVALID_ARG. */
hbi_status hbi_tensor_alloc_aligned(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape,
                                    size_t alignment);

/* Wrap a caller-owned, mutable buffer as a BORROWED tensor. `data` must meet the
 * dtype's natural alignment and `nbytes` must be >= the packed size of `shape`.
 * The module never frees `data`. Fails HBI_ERR_INVALID_ARG. */
hbi_status hbi_tensor_wrap(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape, void *data,
                           size_t nbytes);

/* As hbi_tensor_wrap but marks the tensor read-only (hbi_tensor_data_mut will
 * return NULL). `data` is const because it will not be mutated through this
 * tensor. */
hbi_status hbi_tensor_wrap_readonly(hbi_tensor *out, hbi_dtype dt, const hbi_shape *shape,
                                    const void *data, size_t nbytes);

/* Free the buffer IFF ownership==OWNED, then zero *t. Safe on NULL, on borrowed
 * or view tensors (no free — just zeroes), and if called twice. */
void hbi_tensor_destroy(hbi_tensor *t);

/* Deep-copy `src` into a fresh OWNED, C-contiguous *out (materializing a non-
 * contiguous source into contiguous layout). Quant metadata is copied by value;
 * its scales/zero_points remain BORROWED (same pointers as src — NOT
 * duplicated). Fails HBI_ERR_INVALID_ARG or HBI_ERR_OOM. */
hbi_status hbi_tensor_clone(const hbi_tensor *src, hbi_tensor *out);

/* Copy elements from `src` into an existing `dst` (same dtype and shape). Fails
 * HBI_ERR_STATE (dst read-only), HBI_ERR_INVALID_ARG (NULL, dtype/shape
 * mismatch, insufficient dst capacity). */
hbi_status hbi_tensor_copy_into(const hbi_tensor *src, hbi_tensor *dst);

/* Move ownership from `src` to `dst`: *dst takes src's buffer, then *src is
 * zeroed (C move semantics — exactly one owner remains). `*dst` should be empty
 * or already destroyed. Fails HBI_ERR_INVALID_ARG on NULL. */
hbi_status hbi_tensor_move(hbi_tensor *dst, hbi_tensor *src);

/* ── Accessors (pure; NULL-tolerant) ─────────────────────────────────────── */
hbi_dtype hbi_tensor_dtype(const hbi_tensor *t);
const hbi_shape *hbi_tensor_shape(const hbi_tensor *t);
const hbi_strides *hbi_tensor_strides(const hbi_tensor *t);
size_t hbi_tensor_nbytes(const hbi_tensor *t);
const void *hbi_tensor_cdata(const hbi_tensor *t); /* read pointer; NULL only if empty */
void *hbi_tensor_data_mut(hbi_tensor *t);          /* NULL if read-only or empty */
bool hbi_tensor_is_contiguous(const hbi_tensor *t);
bool hbi_tensor_is_readonly(const hbi_tensor *t);
bool hbi_tensor_owns_data(const hbi_tensor *t);
hbi_tensor_device hbi_tensor_device_of(const hbi_tensor *t);

/* ── Views (zero-copy; always produce ownership==VIEW; never allocate) ────────
 * A view borrows its parent's buffer and inherits read_only. It stores no parent
 * pointer or refcount — it must not outlive the parent's buffer. All fail
 * HBI_ERR_INVALID_ARG on NULL/out-of-range and set the error record. Sub-byte
 * dtypes restrict view ops (see below) because element strides cannot address a
 * nibble. */

/* Full alias with identical metadata. */
hbi_status hbi_tensor_view(const hbi_tensor *parent, hbi_tensor *out);

/* Reinterpret shape without moving data. Requires a CONTIGUOUS parent
 * (HBI_ERR_UNSUPPORTED otherwise) and equal element count (HBI_ERR_INVALID_ARG
 * otherwise). Result is contiguous. Never copies — clone first if you need to. */
hbi_status hbi_tensor_reshape(const hbi_tensor *parent, const hbi_shape *new_shape,
                              hbi_tensor *out);

/* Narrow one `axis` to [start, start+count). Offsets `data`, shrinks that dim,
 * keeps strides (may become non-contiguous). Sub-byte dtypes: `start` must be
 * byte-aligned (HBI_ERR_UNSUPPORTED otherwise). */
hbi_status hbi_tensor_slice(const hbi_tensor *parent, uint32_t axis, int64_t start, int64_t count,
                            hbi_tensor *out);

/* Multi-axis slice: starts[i]/counts[i] apply to axis i for i in [0,n). */
hbi_status hbi_tensor_subtensor(const hbi_tensor *parent, const int64_t *starts,
                                const int64_t *counts, uint32_t n, hbi_tensor *out);

/* Swap two axes (metadata only; no bytes move). Marks the result non-contiguous.
 * Sub-byte dtypes → HBI_ERR_UNSUPPORTED (a nibble-packed layout cannot be
 * transposed without repacking). */
hbi_status hbi_tensor_transpose(const hbi_tensor *parent, uint32_t axis_a, uint32_t axis_b,
                                hbi_tensor *out);

/* General axis permutation `perm[0..n)` (a bijection over the axes). Recomputes
 * contiguity. Sub-byte dtypes → HBI_ERR_UNSUPPORTED unless the permutation is
 * the identity. */
hbi_status hbi_tensor_permute(const hbi_tensor *parent, const uint32_t *perm, uint32_t n,
                              hbi_tensor *out);

/* Drop all size-1 axes. */
hbi_status hbi_tensor_squeeze(const hbi_tensor *parent, hbi_tensor *out);

/* Insert a size-1 axis at `axis`. */
hbi_status hbi_tensor_unsqueeze(const hbi_tensor *parent, uint32_t axis, hbi_tensor *out);

/* ── Quantization metadata ─────────────────────────────────────────────────── */

/* Zero-init *out then set scheme/group. NULL out fails HBI_ERR_INVALID_ARG. */
hbi_status hbi_quant_meta_init(hbi_quant_meta *out, hbi_quant_scheme scheme, int32_t group_size,
                               uint32_t group_axis);

/* Store a BORROWED scales pointer (dtype + element count). */
hbi_status hbi_quant_meta_set_scales(hbi_quant_meta *m, const void *scales, hbi_dtype dt, size_t n);

/* Store a BORROWED zero-points pointer (dtype + element count). */
hbi_status hbi_quant_meta_set_zero_points(hbi_quant_meta *m, const void *zp, hbi_dtype dt,
                                          size_t n);

/* Set sub-byte packing details. */
hbi_status hbi_quant_meta_set_packing(hbi_quant_meta *m, bool offset_binary,
                                      hbi_nibble_order order);

/* Validate metadata against a tensor: group_size divides dims[group_axis] (or is
 * per-tensor), scale/zp counts are consistent with the scheme, and the tensor's
 * dtype is quantizable. Fails HBI_ERR_INVALID_ARG. */
hbi_status hbi_quant_meta_validate(const hbi_quant_meta *m, const hbi_tensor *t);

/* Attach a copy of *m to `t` (inner pointers stay borrowed) after validating. */
hbi_status hbi_tensor_attach_quant(hbi_tensor *t, const hbi_quant_meta *m);

/* Borrowed pointer to the tensor's quant metadata, or NULL if scheme==NONE. */
const hbi_quant_meta *hbi_tensor_quant(const hbi_tensor *t);

/* True iff the tensor carries a quant scheme other than NONE. */
bool hbi_tensor_is_quantized(const hbi_tensor *t);

/* ── Validation ──────────────────────────────────────────────────────────────
 * A full invariant sweep for a tensor: shape valid, strides valid, capacity >=
 * required packed bytes, data aligned to `align`, sub-byte contiguity, quant
 * consistency. Returns the first failing status with a descriptive message. */
hbi_status hbi_tensor_validate(const hbi_tensor *t);

/* True iff t->data is aligned to `alignment` (power of two). */
bool hbi_tensor_is_aligned(const hbi_tensor *t, size_t alignment);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_tensor_name(void);
hbi_status hbi_tensor_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_TENSOR_H */
