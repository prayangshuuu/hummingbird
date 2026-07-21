/* kernel.c — Kernel runtime: op taxonomy, kernel registry, dispatch, and
 * workspace management (RFC-003, DD-025).
 *
 * See kernel.h for the full contract. Implementation notes:
 *   - This TU owns the INTERFACE only: no kernel implementations live here. The
 *     CPU reference kernels register from backends/cpu/.
 *   - The registry is a fixed-size table populated once at init before worker
 *     threads (documented precondition), so it needs no locking — the same
 *     contract the `backend` module uses.
 *   - CORRECTNESS-FIRST: every entry point validates its inputs and returns an
 *     hbi_status; nothing here aborts or calls exit() (DD-011/DD-019).
 */
#include "kernel/kernel_internal.h"

#include "platform/platform.h"

#include <string.h>

/* ── Op taxonomy ─────────────────────────────────────────────────────────── */

const char *hbi_kernel_op_str(hbi_kernel_op op) {
    switch (op) {
    case HBI_KERNEL_OP_COPY:
        return "copy";
    case HBI_KERNEL_OP_FILL:
        return "fill";
    case HBI_KERNEL_OP_CAST:
        return "cast";
    case HBI_KERNEL_OP_TRANSPOSE:
        return "transpose";
    case HBI_KERNEL_OP_ELEMENTWISE:
        return "elementwise";
    case HBI_KERNEL_OP_MATMUL:
        return "matmul";
    case HBI_KERNEL_OP_RESHAPE:
        return "reshape";
    case HBI_KERNEL_OP_REDUCE:
        return "reduce";
    case HBI_KERNEL_OP_BATCHED_MATMUL:
        return "batched_matmul";
    case HBI_KERNEL_OP_SOFTMAX:
        return "softmax";
    case HBI_KERNEL_OP_RMSNORM:
        return "rmsnorm";
    case HBI_KERNEL_OP_LAYERNORM:
        return "layernorm";
    case HBI_KERNEL_OP_ROPE:
        return "rope";
    case HBI_KERNEL_OP_ACTIVATION:
        return "activation";
    case HBI_KERNEL_OP_MOE_ROUTING:
        return "moe_routing";
    case HBI_KERNEL_OP_ATTENTION:
        return "attention";
    case HBI_KERNEL_OP_INVALID:
    case HBI_KERNEL_OP_COUNT:
        break;
    }
    return "invalid";
}

bool hbi_kernel_op_is_valid(hbi_kernel_op op) {
    return op > HBI_KERNEL_OP_INVALID && op < HBI_KERNEL_OP_COUNT;
}

/* ── Args ─────────────────────────────────────────────────────────────────── */

hbi_status hbi_kernel_args_init(hbi_kernel_args *args) {
    if (args == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "kernel_args_init: NULL args");
    }
    memset(args, 0, sizeof(*args));
    return HBI_OK;
}

bool hbi_kernel_supports_dtype(const hbi_kernel *k, hbi_dtype dt) {
    if (k == NULL || k->supported_dtypes == NULL) {
        return false;
    }
    for (size_t i = 0; i < k->num_dtypes; ++i) {
        if (k->supported_dtypes[i] == dt) {
            return true;
        }
    }
    return false;
}

/* ── Workspace ───────────────────────────────────────────────────────────────
 * A reusable aligned buffer. `reserve` reallocates only when the request does
 * not fit the current buffer at the requested alignment, so a warm workspace
 * performs no allocation. */

hbi_status hbi_kernel_workspace_init(hbi_kernel_workspace *ws, hbi_allocator *allocator) {
    if (ws == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "workspace_init: NULL ws");
    }
    memset(ws, 0, sizeof(*ws));
    ws->allocator = (allocator != NULL) ? allocator : hbi_allocator_system();
    return HBI_OK;
}

hbi_status hbi_kernel_workspace_reserve(hbi_kernel_workspace *ws, size_t bytes, size_t alignment) {
    if (ws == NULL || ws->allocator == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "workspace_reserve: NULL ws/allocator");
    }
    if (alignment != 0 && !hbi_is_pow2(alignment)) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "workspace_reserve: bad alignment %zu",
                            alignment);
    }
    if (bytes == 0) {
        return HBI_OK; /* nothing to guarantee */
    }
    /* Reuse when the existing buffer already covers size AND alignment. */
    bool aligned_ok =
        (alignment == 0) || (ws->buffer != NULL && ((uintptr_t)ws->buffer % alignment) == 0);
    if (ws->buffer != NULL && ws->capacity >= bytes && aligned_ok) {
        return HBI_OK;
    }
    void *fresh = hbi_alloc(ws->allocator, bytes, alignment, HBI_MEM_SCRATCH);
    if (fresh == NULL) {
        return HBI_ERR_SETF(HBI_ERR_OOM, hbi_os_errno(), "workspace_reserve: %zu bytes failed",
                            bytes);
    }
    if (ws->buffer != NULL) {
        hbi_free(ws->allocator, ws->buffer);
    }
    ws->buffer = fresh;
    ws->capacity = bytes;
    ws->alignment = (alignment == 0) ? 1u : alignment;
    return HBI_OK;
}

void *hbi_kernel_workspace_ptr(const hbi_kernel_workspace *ws) {
    return ws == NULL ? NULL : ws->buffer;
}

size_t hbi_kernel_workspace_capacity(const hbi_kernel_workspace *ws) {
    return ws == NULL ? 0u : ws->capacity;
}

void hbi_kernel_workspace_reset(hbi_kernel_workspace *ws) {
    if (ws == NULL || ws->buffer == NULL) {
        return;
    }
    hbi_free(ws->allocator, ws->buffer);
    ws->buffer = NULL;
    ws->capacity = 0;
    ws->alignment = 0;
}

void hbi_kernel_workspace_destroy(hbi_kernel_workspace *ws) {
    if (ws == NULL) {
        return;
    }
    hbi_kernel_workspace_reset(ws);
    memset(ws, 0, sizeof(*ws));
}

/* ── Registry ────────────────────────────────────────────────────────────────
 * Fixed table; registration at init before threads (no locking). */

static const hbi_kernel *g_kernels[HBI_KERNEL_REGISTRY_MAX];
static int g_kernel_count;

/* Does an already-registered kernel collide with `k` on (op, device, dtype)?
 * Two kernels for the same op+device must not both claim the same dtype — that
 * would make resolution order-dependent. Layout flags are allowed to differ
 * (a contiguous-only and a general kernel can coexist and resolve by key). */
static bool collides(const hbi_kernel *k) {
    for (int i = 0; i < g_kernel_count; ++i) {
        const hbi_kernel *e = g_kernels[i];
        if (e->op != k->op || e->device != k->device) {
            continue;
        }
        for (size_t a = 0; a < k->num_dtypes; ++a) {
            if (hbi_kernel_supports_dtype(e, k->supported_dtypes[a]) &&
                e->layout_flags == k->layout_flags) {
                return true;
            }
        }
    }
    return false;
}

hbi_status hbi_kernel_register(const hbi_kernel *k) {
    if (k == NULL || k->run == NULL || k->name == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "kernel_register: NULL kernel/run/name");
    }
    if (!hbi_kernel_op_is_valid(k->op)) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "kernel_register: invalid op %d for '%s'",
                            (int)k->op, k->name);
    }
    if (k->supported_dtypes == NULL || k->num_dtypes == 0) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "kernel_register: '%s' has empty dtype set",
                            k->name);
    }
    if (g_kernel_count >= HBI_KERNEL_REGISTRY_MAX) {
        return HBI_ERR_SETF(HBI_ERR_STATE, 0, "kernel_register: registry full (%d)",
                            HBI_KERNEL_REGISTRY_MAX);
    }
    if (collides(k)) {
        return HBI_ERR_SETF(HBI_ERR_STATE, 0,
                            "kernel_register: '%s' collides with an existing (op,device,dtype)",
                            k->name);
    }
    g_kernels[g_kernel_count++] = k;
    return HBI_OK;
}

void hbi_kernel_registry_clear(void) {
    memset(g_kernels, 0, sizeof(g_kernels));
    g_kernel_count = 0;
}

int hbi_kernel_registry_count(void) {
    return g_kernel_count;
}

const hbi_kernel *hbi_kernel_at(int index) {
    if (index < 0 || index >= g_kernel_count) {
        return NULL;
    }
    return g_kernels[index];
}

/* ── Dispatch ────────────────────────────────────────────────────────────── */

hbi_status hbi_kernel_resolve(const hbi_kernel_key *key, const hbi_kernel **out) {
    if (key == NULL || out == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "kernel_resolve: NULL arg");
    }
    if (!hbi_kernel_op_is_valid(key->op)) {
        return HBI_ERR_SETF(HBI_ERR_INVALID_ARG, 0, "kernel_resolve: invalid op %d", (int)key->op);
    }
    for (int i = 0; i < g_kernel_count; ++i) {
        const hbi_kernel *k = g_kernels[i];
        if (k->op != key->op || k->device != key->device) {
            continue;
        }
        if (!hbi_kernel_supports_dtype(k, key->dtype)) {
            continue;
        }
        /* The kernel must satisfy every layout flag the caller requires. A
         * kernel that requires contiguity cannot serve a key that does not (it
         * would compute the wrong thing); a general kernel serves any key. */
        if ((k->layout_flags & ~key->layout_flags) != 0u) {
            continue;
        }
        *out = k;
        return HBI_OK;
    }
    return HBI_ERR_SETF(HBI_ERR_NOT_FOUND, 0,
                        "kernel_resolve: no kernel for op=%s device=%d dtype=%s",
                        hbi_kernel_op_str(key->op), (int)key->device, hbi_dtype_str(key->dtype));
}

/* The dtype a call operates on: the first input's, or (for ops with no inputs,
 * e.g. FILL) the first output's. */
static hbi_status infer_dtype(const hbi_kernel_args *args, hbi_dtype *out) {
    if (args->num_inputs > 0 && args->inputs[0] != NULL) {
        *out = args->inputs[0]->dtype;
        return HBI_OK;
    }
    if (args->num_outputs > 0 && args->outputs[0] != NULL) {
        *out = args->outputs[0]->dtype;
        return HBI_OK;
    }
    return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "kernel_dispatch: no tensor to infer dtype from");
}

hbi_status hbi_kernel_dispatch(hbi_kernel_op op, hbi_tensor_device device,
                               const hbi_kernel_args *args, hbi_kernel_workspace *ws) {
    if (args == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "kernel_dispatch: NULL args");
    }
    if (args->num_inputs > HBI_KERNEL_MAX_INPUTS || args->num_outputs > HBI_KERNEL_MAX_OUTPUTS) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "kernel_dispatch: operand count out of range");
    }
    hbi_dtype dt = HBI_DTYPE_INVALID;
    hbi_status st = infer_dtype(args, &dt);
    if (st != HBI_OK) {
        return st;
    }

    hbi_kernel_key key = {
        .op = op,
        .device = device,
        .dtype = dt,
        .layout_flags = HBI_KERNEL_LAYOUT_ANY,
    };
    const hbi_kernel *k = NULL;
    st = hbi_kernel_resolve(&key, &k);
    if (st != HBI_OK) {
        return st;
    }

    /* Determine the kernel's scratch need for this call. */
    size_t need = 0;
    if (k->workspace_size != NULL) {
        st = k->workspace_size(args, &need);
        if (st != HBI_OK) {
            return st;
        }
    }

    /* Use the caller's workspace when given; otherwise a temporary one over the
     * system allocator, released after the call. */
    hbi_kernel_workspace local;
    hbi_kernel_workspace *use = ws;
    bool own_local = false;
    if (need > 0 && use == NULL) {
        st = hbi_kernel_workspace_init(&local, NULL);
        if (st != HBI_OK) {
            return st;
        }
        use = &local;
        own_local = true;
    }
    if (need > 0) {
        st = hbi_kernel_workspace_reserve(use, need, HBI_TENSOR_DEFAULT_ALIGN);
        if (st != HBI_OK) {
            if (own_local) {
                hbi_kernel_workspace_destroy(&local);
            }
            return st;
        }
    }

    st = k->run(args, use);

    if (own_local) {
        hbi_kernel_workspace_destroy(&local);
    }
    return st;
}

/* ── Module identity / self-test ─────────────────────────────────────────────
 * A quick invariant sweep the CTest scaffold and higher layers can call without
 * a full unit-test run. Does NOT depend on any kernel being registered. */
const char *hbi_kernel_name(void) {
    return "kernel";
}

hbi_status hbi_kernel_selftest(void) {
    /* Op-string table is complete: every valid op has a non-"invalid" spelling,
     * and the guards return "invalid". */
    for (int op = HBI_KERNEL_OP_INVALID + 1; op < HBI_KERNEL_OP_COUNT; ++op) {
        const char *s = hbi_kernel_op_str((hbi_kernel_op)op);
        if (s == NULL || strcmp(s, "invalid") == 0) {
            return HBI_ERR_SETF(HBI_ERR_INTERNAL, 0, "kernel_selftest: op %d has no name", op);
        }
        if (!hbi_kernel_op_is_valid((hbi_kernel_op)op)) {
            return HBI_ERR_SETF(HBI_ERR_INTERNAL, 0, "kernel_selftest: op %d not valid", op);
        }
    }
    if (hbi_kernel_op_is_valid(HBI_KERNEL_OP_INVALID) ||
        hbi_kernel_op_is_valid(HBI_KERNEL_OP_COUNT)) {
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "kernel_selftest: sentinel op reported valid");
    }

    /* A workspace round-trips init → reserve → reuse → reset without leaking. */
    hbi_kernel_workspace ws;
    hbi_status st = hbi_kernel_workspace_init(&ws, NULL);
    if (st != HBI_OK) {
        return st;
    }
    st = hbi_kernel_workspace_reserve(&ws, 256, 64);
    if (st != HBI_OK) {
        hbi_kernel_workspace_destroy(&ws);
        return st;
    }
    void *first = hbi_kernel_workspace_ptr(&ws);
    if (first == NULL || ((uintptr_t)first % 64u) != 0u) {
        hbi_kernel_workspace_destroy(&ws);
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "kernel_selftest: workspace misaligned");
    }
    /* A smaller request must reuse the same buffer (no reallocation). */
    st = hbi_kernel_workspace_reserve(&ws, 128, 64);
    if (st != HBI_OK || hbi_kernel_workspace_ptr(&ws) != first) {
        hbi_kernel_workspace_destroy(&ws);
        return HBI_ERR_SET(HBI_ERR_INTERNAL, 0, "kernel_selftest: workspace did not reuse buffer");
    }
    hbi_kernel_workspace_destroy(&ws);
    return HBI_OK;
}
