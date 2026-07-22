/* executor.h — Walks a forward graph and dispatches each op node to its typed module.
 *
 * Core-public header for the `executor` module (layer 7).
 * Symbols are prefixed `hbi_` (internal, no stability guarantee).
 * See docs/architecture/10-execution-graph.md.
 */
#ifndef HB_EXECUTOR_H
#define HB_EXECUTOR_H

#include "common/common.h"
#include "graph/graph.h"
#include "kernel/kernel.h"
#include "memory/memory.h"
#include "planner/planner.h"
#include "tensor/tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hbi_executor hbi_executor;
typedef struct hbi_exec_context hbi_exec_context;

/* ── Execution Context ─────────────────────────────────────────────────────── */

/* Create a runtime context for a graph. The context holds the actual tensor
 * data mapped to graph values, and the workspace buffer.
 * It borrows the graph, which must outlive the context. */
hbi_status hbi_exec_context_create(const hbi_graph *graph, hbi_allocator *allocator,
                                   hbi_exec_context **out);

/* Bind a tensor to a specific value ID in the graph. The tensor is BORROWED
 * and must outlive the context. */
hbi_status hbi_exec_context_bind(hbi_exec_context *ctx, uint32_t value_id, hbi_tensor *tensor);

/* Allocate all unbound intermediate tensors in the context using its allocator,
 * guided by the provided memory plan (which manages buffer aliasing).
 * This should be called after binding inputs and constants. */
hbi_status hbi_exec_context_allocate_internals(hbi_exec_context *ctx, const hbi_memory_plan *plan);

/* Destroy the context and free any tensors it allocated (but not borrowed ones). */
void hbi_exec_context_destroy(hbi_exec_context *ctx);

/* ── Executor ──────────────────────────────────────────────────────────────── */

/* Create an executor for a specific graph. Resolves kernel implementations. */
hbi_status hbi_executor_create(const hbi_graph *graph, hbi_executor **out);

/* Run the graph sequentially using the tensors bound in the context. */
hbi_status hbi_executor_run(const hbi_executor *executor, hbi_exec_context *ctx);

/* Destroy the executor. */
void hbi_executor_destroy(hbi_executor *executor);

/* ── Module Identity ───────────────────────────────────────────────────────── */

/* Human-readable module name. Never NULL. */
const char *hbi_executor_name(void);

/* Compile-time self-check. Returns HBI_OK when the module is well-formed. */
hbi_status hbi_executor_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HB_EXECUTOR_H */
