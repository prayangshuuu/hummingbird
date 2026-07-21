/* kernel.h — Kernel runtime: the backend-agnostic compute abstraction (RFC-003,
 * DD-025). This is the EXECUTION layer for every mathematical operation in the
 * engine: it defines HOW a computation is described (a typed op + operands +
 * params) and HOW it is dispatched to a concrete kernel, without knowing which
 * backend answers. It is deliberately model-independent — no inference logic, no
 * model geometry, no scheduling (that is the executor/scheduler, higher layers).
 *
 * Core-public header for the `kernel` module (layer 4). Symbols are prefixed
 * `hbi_` (internal, no stability guarantee); external embedders use
 * <hummingbird/hummingbird.h>. See docs/architecture/03-dependency-graph.md.
 *
 * ── Separation of interface and implementation (RFC-003) ─────────────────────
 * This module owns the INTERFACE (op taxonomy, the kernel descriptor, the call-
 * argument block, dispatch, the kernel registry, and workspace management). It
 * owns NO kernel implementations. Concrete kernels live in a backend — the CPU
 * reference kernels are in `backends/cpu/` and REGISTER themselves here at init.
 * A backend depends on `kernel`; `kernel` never depends on a backend. This keeps
 * CPU and future GPU implementations able to coexist behind one dispatch surface.
 *
 * ── Layering (why there is no `backend`/`device` dependency) ─────────────────
 * `kernel` is layer 4. `backend` (DD-007 lifecycle registry) is also layer 4, so
 * depending on it would be a forbidden lateral edge — and the flow is the other
 * way regardless (a backend registers kernels here). `device` (layer 3) SIMD-
 * level dispatch is an M2 concern; the dispatch key reserves a capability field
 * for it without linking `device` today.
 *
 * ── Correctness-first (RFC-003) ──────────────────────────────────────────────
 * No SIMD, no accelerator, no fast paths yet. Kernels are scalar reference
 * implementations whose only job is to be provably correct. Every kernel and
 * every dispatch call VALIDATES its inputs and returns an hbi_status (DD-011);
 * none ever aborts or calls exit() (DD-019). Optimization is a later milestone.
 *
 * ── Thread-safety ────────────────────────────────────────────────────────────
 * The registry is populated once at startup, before worker threads (documented
 * precondition, no locking) — exactly the `backend` module's contract. Resolve
 * and dispatch are read-only over the registry and safe to call concurrently.
 * An hbi_kernel_workspace is NOT thread-safe: give each worker its own (like an
 * arena). The error record is thread-local, so diagnostics never race.
 */
#ifndef HB_KERNEL_H
#define HB_KERNEL_H

#include "common/common.h"
#include "memory/memory.h"
#include "tensor/tensor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum operands a single kernel call may take. Fixed so hbi_kernel_args is a
 * POD value type with no heap indirection. Two inputs + one output covers every
 * op in this phase (matmul, elementwise); raised later if an op needs more. */
#define HBI_KERNEL_MAX_INPUTS 4u
#define HBI_KERNEL_MAX_OUTPUTS 2u

/* ── Operation taxonomy ──────────────────────────────────────────────────────
 * Every operation the runtime can express. Append-only (values are an internal
 * contract; never renumber). Elementwise/reduce/activation are ONE op each,
 * parameterized by a *kind* in the params block (DD-004's typed-op idea), so the
 * op set stays small. Only COPY/FILL/CAST/TRANSPOSE/ELEMENTWISE/MATMUL have a
 * registered CPU kernel in this phase; the rest are declared so the dispatch and
 * registry surface is complete and future-proof, and resolve cleanly to
 * HBI_ERR_NOT_FOUND until their kernels land. */
typedef enum hbi_kernel_op {
    HBI_KERNEL_OP_INVALID = 0,    /* zero-init guard; never a usable op */
    HBI_KERNEL_OP_COPY,           /* [BUILT] element copy (same dtype+shape) */
    HBI_KERNEL_OP_FILL,           /* [BUILT] write a scalar into every element */
    HBI_KERNEL_OP_CAST,           /* [BUILT] elementwise dtype conversion */
    HBI_KERNEL_OP_TRANSPOSE,      /* [BUILT] materializing 2-axis transpose */
    HBI_KERNEL_OP_ELEMENTWISE,    /* [BUILT] add/mul (see hbi_elementwise_kind) */
    HBI_KERNEL_OP_MATMUL,         /* [BUILT] 2-D [M,K]x[K,N] -> [M,N] */
    HBI_KERNEL_OP_RESHAPE,        /* [PLANNED] */
    HBI_KERNEL_OP_REDUCE,         /* [PLANNED] sum/max/... */
    HBI_KERNEL_OP_BATCHED_MATMUL, /* [PLANNED] */
    HBI_KERNEL_OP_SOFTMAX,        /* [PLANNED] */
    HBI_KERNEL_OP_RMSNORM,        /* [PLANNED] */
    HBI_KERNEL_OP_LAYERNORM,      /* [PLANNED] */
    HBI_KERNEL_OP_ROPE,           /* [PLANNED] */
    HBI_KERNEL_OP_ACTIVATION,     /* [PLANNED] gelu/silu/... */
    HBI_KERNEL_OP_MOE_ROUTING,    /* [PLANNED] */
    HBI_KERNEL_OP_ATTENTION,      /* [PLANNED] future attention variants */
    HBI_KERNEL_OP_COUNT           /* sentinel: number of ops (not an op) */
} hbi_kernel_op;

/* Stable lower-case spelling ("matmul","cast",…). Never NULL; "invalid" for an
 * out-of-range value. Safe on any thread, cannot fail. */
const char *hbi_kernel_op_str(hbi_kernel_op op);

/* True for a real, usable op value (excludes INVALID and COUNT). */
bool hbi_kernel_op_is_valid(hbi_kernel_op op);

/* ── Op-parameter kinds ──────────────────────────────────────────────────────
 * The discriminants for the multi-purpose ops. Append-only. */
typedef enum hbi_elementwise_kind {
    HBI_ELEMENTWISE_ADD = 0,
    HBI_ELEMENTWISE_MUL,
    HBI_ELEMENTWISE_KIND_COUNT
} hbi_elementwise_kind;

typedef enum hbi_reduce_kind {
    HBI_REDUCE_SUM = 0,
    HBI_REDUCE_MAX,
    HBI_REDUCE_KIND_COUNT
} hbi_reduce_kind;

typedef enum hbi_activation_kind {
    HBI_ACTIVATION_GELU = 0,
    HBI_ACTIVATION_SILU,
    HBI_ACTIVATION_RELU,
    HBI_ACTIVATION_KIND_COUNT
} hbi_activation_kind;

/* ── Layout / capability flags ───────────────────────────────────────────────
 * A bitmask a kernel advertises (in hbi_kernel.layout_flags) and a caller can
 * require (in hbi_kernel_key.layout_flags). Resolution matches a kernel only if
 * it satisfies every required flag. Reserved bits leave room for SIMD-level or
 * device-capability constraints (M2) without an ABI change. */
typedef enum hbi_kernel_layout_flags {
    HBI_KERNEL_LAYOUT_ANY = 0u,               /* no constraint */
    HBI_KERNEL_LAYOUT_REQUIRE_CONTIGUOUS = 1u /* operands must be C-contiguous */
} hbi_kernel_layout_flags;

/* ── Call parameters (op-specific scalars) ───────────────────────────────────
 * A small tagged union carrying the non-tensor operands of a call. Which member
 * is read is fixed by the op. Kept a plain value type (no pointers) so args are
 * cheap to build on the stack. */
typedef struct hbi_kernel_params {
    union {
        double fill_value;                /* FILL: value written to every elem */
        hbi_dtype cast_target;            /* CAST: destination dtype */
        hbi_elementwise_kind elementwise; /* ELEMENTWISE: add/mul */
        hbi_reduce_kind reduce;           /* REDUCE: sum/max */
        hbi_activation_kind activation;   /* ACTIVATION: gelu/silu/... */
        struct {                          /* TRANSPOSE: the two axes to swap */
            uint32_t axis_a;
            uint32_t axis_b;
        } transpose;
    } u;
} hbi_kernel_params;

/* ── Call arguments ──────────────────────────────────────────────────────────
 * A uniform, op-independent operand block so dispatch has one signature for
 * every op. Inputs are read-only tensors; outputs are pre-allocated by the
 * caller (kernels never allocate their outputs — the "no alloc on the hot path"
 * invariant, §7). All pointers are BORROWED; the args struct owns nothing. */
typedef struct hbi_kernel_args {
    const hbi_tensor *inputs[HBI_KERNEL_MAX_INPUTS];
    uint32_t num_inputs;
    hbi_tensor *outputs[HBI_KERNEL_MAX_OUTPUTS];
    uint32_t num_outputs;
    hbi_kernel_params params;
} hbi_kernel_args;

/* Zero-init *args. Convenience so callers do not forget a field. NULL fails
 * HBI_ERR_INVALID_ARG. */
hbi_status hbi_kernel_args_init(hbi_kernel_args *args);

/* ── Workspace ───────────────────────────────────────────────────────────────
 * A reusable, aligned scratch buffer a kernel may need for temporaries. Backed
 * by an hbi_allocator (typically a per-worker arena, tagged HBI_MEM_SCRATCH).
 * `reserve` grows the buffer only when the request exceeds the current capacity,
 * so a warmed workspace performs NO allocation on the steady state — the hot-
 * path invariant. Not thread-safe: one workspace per worker. The reference
 * kernels in this phase need zero workspace, but the machinery exists and is
 * tested so async/tiled kernels can use it later without an interface change. */
typedef struct hbi_kernel_workspace {
    hbi_allocator *allocator; /* BORROWED; source of the backing buffer */
    void *buffer;             /* current backing block (owned via allocator) */
    size_t capacity;          /* bytes available in `buffer` */
    size_t alignment;         /* alignment `buffer` is guaranteed to */
} hbi_kernel_workspace;

/* Bind a workspace to an allocator (system allocator if `allocator` is NULL).
 * Starts empty (no buffer). Fails HBI_ERR_INVALID_ARG on NULL out. */
hbi_status hbi_kernel_workspace_init(hbi_kernel_workspace *ws, hbi_allocator *allocator);

/* Ensure the workspace can supply at least `bytes` at `alignment` (power of two;
 * 0 means natural). Reuses the existing buffer when it already fits; otherwise
 * frees and reallocates. `bytes == 0` is a no-op success. Fails
 * HBI_ERR_INVALID_ARG (NULL/bad alignment) or HBI_ERR_OOM. */
hbi_status hbi_kernel_workspace_reserve(hbi_kernel_workspace *ws, size_t bytes, size_t alignment);

/* Borrowed pointer to the backing buffer (NULL if nothing reserved) and its
 * usable capacity. */
void *hbi_kernel_workspace_ptr(const hbi_kernel_workspace *ws);
size_t hbi_kernel_workspace_capacity(const hbi_kernel_workspace *ws);

/* Release the backing buffer (capacity → 0). The workspace stays bound to its
 * allocator and can be reserved again. NULL-safe. */
void hbi_kernel_workspace_reset(hbi_kernel_workspace *ws);

/* Release the buffer and unbind; zeroes *ws. NULL-safe, idempotent. */
void hbi_kernel_workspace_destroy(hbi_kernel_workspace *ws);

/* ── Kernel descriptor ───────────────────────────────────────────────────────
 * The metadata + execution interface a backend exposes for one (op, device,
 * dtype-set) it can compute. Registered once at init; the dispatch layer matches
 * calls against these. The implementation (the scalar loops) is hidden in the
 * backend's .c file — only this descriptor crosses the module boundary.
 *
 * `run` must validate its args (tensor compatibility, dtype, layout) and return
 * a standardized status; it must never crash on bad input. `workspace_size`
 * reports the scratch bytes `run` needs for the given args (0 for the reference
 * kernels). `supported_dtypes` points at a static array the kernel owns; the
 * registry stores the pointer (does not copy). */
typedef struct hbi_kernel hbi_kernel; /* fwd for the fn-ptr typedefs */

typedef hbi_status (*hbi_kernel_run_fn)(const hbi_kernel_args *args, hbi_kernel_workspace *ws);
typedef hbi_status (*hbi_kernel_workspace_size_fn)(const hbi_kernel_args *args, size_t *out_bytes);

struct hbi_kernel {
    hbi_kernel_op op;                            /* which operation */
    const char *name;                            /* stable id, e.g. "cpu.matmul.fp32" */
    hbi_tensor_device device;                    /* target device (CPU today) */
    const hbi_dtype *supported_dtypes;           /* BORROWED static array; never NULL */
    size_t num_dtypes;                           /* length of supported_dtypes (>= 1) */
    uint32_t layout_flags;                       /* hbi_kernel_layout_flags this kernel needs */
    hbi_kernel_workspace_size_fn workspace_size; /* may be NULL == always 0 */
    hbi_kernel_run_fn run;                       /* execution interface; never NULL */
};

/* True iff `k` advertises support for element type `dt`. */
bool hbi_kernel_supports_dtype(const hbi_kernel *k, hbi_dtype dt);

/* ── Registry (dynamic backend registration) ─────────────────────────────────
 * Backends register the kernels they provide at startup. A fixed-capacity table
 * is enough for the bootstrap (a handful of backends × a handful of ops);
 * dynamic growth arrives with plug-in loading (DD-007). Precondition: called at
 * init time, before worker threads start — no locking. */

/* Register a kernel descriptor. The descriptor (and its supported_dtypes array)
 * must have static lifetime — the registry stores the pointer, not a copy.
 * Fails HBI_ERR_INVALID_ARG (NULL / no run fn / invalid op / empty dtype set),
 * HBI_ERR_STATE (registry full OR a kernel with the same (op,device,dtype) is
 * already registered — no silent shadowing). */
hbi_status hbi_kernel_register(const hbi_kernel *k);

/* Remove every registered kernel. For test isolation and clean shutdown; call
 * only when no dispatch is in flight. */
void hbi_kernel_registry_clear(void);

/* Number of currently registered kernels. */
int hbi_kernel_registry_count(void);

/* Registered kernel at [index], or NULL if out of range. */
const hbi_kernel *hbi_kernel_at(int index);

/* ── Dispatch ────────────────────────────────────────────────────────────────
 * The runtime NEVER hardcodes a backend: it builds a key and asks the registry
 * for a matching kernel. Resolution is separate from execution so the executor
 * can resolve once (at graph-build time) and cache the pointer, paying no lookup
 * on the decode hot path. */

/* The selection key: an op on a device over a dtype, with layout requirements. */
typedef struct hbi_kernel_key {
    hbi_kernel_op op;
    hbi_tensor_device device;
    hbi_dtype dtype;
    uint32_t layout_flags; /* required hbi_kernel_layout_flags */
} hbi_kernel_key;

/* Find the first registered kernel matching `key`: same op+device, advertising
 * `dtype`, and satisfying every required layout flag. Writes the borrowed
 * descriptor to *out on success. Fails HBI_ERR_INVALID_ARG (NULL / invalid op)
 * or HBI_ERR_NOT_FOUND (no match — sets a descriptive error record). */
hbi_status hbi_kernel_resolve(const hbi_kernel_key *key, const hbi_kernel **out);

/* Resolve a kernel for (op, device, dtype-inferred-from-args), then run it,
 * managing `ws` for the kernel's reported workspace need. `ws` may be NULL, in
 * which case a temporary system-allocator workspace is used for the call and
 * released after (convenience for callers with no reusable scratch). The dtype
 * and layout requirement are inferred from the first input (or first output for
 * ops with no inputs, e.g. FILL). Returns the kernel's status, or
 * HBI_ERR_NOT_FOUND / HBI_ERR_INVALID_ARG on a resolution/validation failure.
 * Never crashes on invalid input. */
hbi_status hbi_kernel_dispatch(hbi_kernel_op op, hbi_tensor_device device,
                               const hbi_kernel_args *args, hbi_kernel_workspace *ws);

/* ── Module identity / self-test ─────────────────────────────────────────── */
const char *hbi_kernel_name(void);
hbi_status hbi_kernel_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_KERNEL_H */
