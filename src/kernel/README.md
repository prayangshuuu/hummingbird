# `kernel` module

The backend-agnostic **compute abstraction** (RFC-003, DD-025): the execution
layer every mathematical operation in the engine is expressed and dispatched
through. It defines *how* a computation is described (a typed op + operands +
params) and *how* a concrete kernel is selected, without knowing which backend
answers. It is model-independent — no inference logic, no model geometry, no
scheduling (those are higher layers).

## Status

`[BUILT]` — op taxonomy, kernel descriptor, args block, workspace management,
kernel registry, and dispatch/resolve. **Interface only** — this module owns no
kernel implementations. The CPU reference kernels live in `backends/cpu/` and
register here at backend init. Correctness-first: no SIMD, no accelerator, no
fast paths yet (that is M2+).

## What it provides

- **Op taxonomy** (`hbi_kernel_op`) — every operation the runtime can express.
  Copy, Fill, Cast, Transpose, Elementwise, and MatMul have registered CPU
  kernels today; Reshape, Reduce, BatchedMatMul, Softmax, RMSNorm, LayerNorm,
  RoPE, Activation, MoE-Routing, and Attention are declared so the dispatch and
  registry surface is complete and future-proof (they resolve to
  `HBI_ERR_NOT_FOUND` until their kernels land).
- **Kernel descriptor** (`hbi_kernel`) — the metadata + execution interface a
  backend exposes for one (op, device, dtype-set): supported dtypes, layout
  flags, an optional workspace-size hook, and the `run` function. The scalar
  loops are hidden in the backend `.c`; only this descriptor crosses the boundary.
- **Call block** (`hbi_kernel_args` + `hbi_kernel_params`) — a uniform,
  op-independent operand block (borrowed input/output tensors + a small tagged
  union of scalar params) so dispatch has one signature for every op.
- **Workspace** (`hbi_kernel_workspace`) — a reusable, aligned scratch buffer a
  kernel may need for temporaries. `reserve` grows only on demand, so a warmed
  workspace performs no allocation on the steady state. One workspace per worker
  (not thread-safe).
- **Registry** — backends register their kernels at startup (before threads, no
  locking). Duplicate (op, device, dtype, layout) registration is refused — no
  silent shadowing.
- **Dispatch / resolve** — the runtime builds a key (op + device + dtype +
  layout flags) and asks the registry for a match. Resolution is separate from
  execution so the executor can resolve once and cache the descriptor, paying no
  lookup on the decode hot path. `hbi_kernel_dispatch` resolves + runs +
  manages the workspace in one call.

Every entry point validates its inputs and returns an `hbi_status`; none aborts
or calls `exit()` (DD-011/DD-019). Nothing here ever crashes on bad input.

## Layout

| File | Role |
|------|------|
| `kernel.h` | Op taxonomy, descriptor, args, workspace, registry, dispatch (`hbi_*`). |
| `kernel_internal.h` | Private header — registry capacity. |
| `kernel.c` | Taxonomy tables, registry, resolve/dispatch, workspace. |
| `kernel_test.c` | Interface unit tests (CTest target `kernel`). |
| `CMakeLists.txt` | Build target `hb_kernel`. |

## Allowed dependencies

`common`, `platform`, `tensor`, `memory`. (Failures are reported through the
`common` error record, so no dependency on `logging`.)

## Forbidden dependencies

The sibling `backend` module (layer 4) — a backend depends on `kernel`, never the
reverse, so depending on it would be a forbidden lateral edge. `device` (SIMD
dispatch) is an M2 concern; the layout/capability flags reserve room for it
without a link today. Also never: any higher-layer module, frontends, or tools.
See `docs/architecture/03-dependency-graph.md` and `09-kernel-runtime.md`.
