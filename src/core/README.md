# `core` module

The **Runtime Context**: lifecycle, foundation-subsystem ownership, and a
subsystem registry for an embeddable engine instance (ADR DD-021).

## What it is

`hbi_core` is the single explicit object that owns the foundation subsystems
every future part of the engine needs — the active allocator, the effective
configuration, the logger's level, the host device report, the profiler
toggle, and the generic thread pool. It brings them up in dependency order and
tears them down in reverse, exactly once, behind an inspectable state machine
(`UNINIT → READY → DEAD`).

There are **no hidden globals**: everything hangs off the `hbi_core` handle the
caller holds — the deliberate departure from Colibrì's process-wide `Model`
god-struct.

## What it is NOT

It is **not** the forward-pass orchestrator. Running a model, sequencing
layers, and producing logits belong to the `runtime` module (layer 10), which
sits *above* this and borrows the context. Separating lifecycle (low,
foundational) from orchestration (high) is exactly why this module is its own
layer (DD-021).

## Layout

| File | Role |
|------|------|
| `core.h` | Public API (`hbi_core_*`): config, lifecycle, accessors, registry. |
| `core_internal.h` | Private struct layout and registry entry type. |
| `core.c` | Implementation: ordered bring-up/tear-down, registry. |
| `core_test.c` | Unit tests (CTest target `core`). |
| `CMakeLists.txt` | Build target `hb_core`. |

## Usage

```c
hbi_core_config cfg;
hbi_core_config_default(&cfg);       /* INFO logs, workers = cores, sys alloc */
cfg.num_workers = 4;

hbi_core *core = NULL;
if (hbi_core_create(&core, &cfg) != HBI_OK) { /* inspect hbi_error_last() */ }

hbi_allocator  *alloc = hbi_core_allocator(core);
hbi_threadpool *pool  = hbi_core_threadpool(core);

/* Attach a higher-layer subsystem without core knowing its type. */
hbi_core_register(core, "my.subsystem", ptr, my_fini);
void *p = hbi_core_lookup(core, "my.subsystem");

hbi_core_destroy(core);              /* reverse-order teardown, runs finalizers */
```

## Allowed dependencies

`common`, `platform`, `logging`, `config`, `profiler`, `device`, `memory`,
`threadpool`. It composes the whole foundation and therefore depends downward
on all of it.

## Forbidden dependencies

Anything above the foundation: `runtime`, `executor`, `scheduler`, `model`,
`backend`, `context`, etc. The orchestrator depends on `core`, never the
reverse — that would create a cycle. Frontends and tools depend on the public
library, never on this directly. See `docs/architecture/03-dependency-graph.md`.
