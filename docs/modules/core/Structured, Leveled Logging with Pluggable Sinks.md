---
kind: logging_system
name: Structured, Leveled Logging with Pluggable Sinks
category: logging_system
scope:
    - '**'
source_files:
    - src/logging/logging.h
    - src/logging/logging.c
    - src/logging/logging_internal.h
    - src/logging/logging_test.c
    - src/core/core.c
---

## What system/approach is used
Hummingbird ships a lightweight, zero-dependency C17 logging subsystem (`src/logging/`) that provides:
- Six severity levels: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `PROFILING` (ordered so numeric comparison implements filtering; `PROFILING` is an explicit opt-in stream never gated by the severity floor).
- Structured key/value fields attached to each record (up to 8 per emit) so sinks can render them without reparsing the message.
- A pluggable sink interface (`hbi_log_sink_fn`) with two built-ins: a one-line text formatter to stderr and a JSON formatter. Custom sinks are installed once at startup via `hbi_log_set_sink`.
- Compile-time level stripping via `HB_LOG_COMPILE_LEVEL` — lower levels expand to nothing so release builds pay zero cost for TRACE/DEBUG.
- Runtime level control through an atomic global minimum level, defaulting to INFO.
- Thread-safe emits (sink output serialized by a lazily-initialized mutex); sink installation is not thread-safe.
- Separate from telemetry: logging is human-readable diagnostics; machine-readable timings belong to the profiler module.

No external logging library is used; everything is implemented in ~250 lines of C using only `common` and `platform` primitives.

## Key files and packages
- `src/logging/logging.h` — Public API: level enum, field/record structs, sink typedef, `hbi_log_*` functions, `HB_LOG_*` macros.
- `src/logging/logging.c` — Implementation: level filtering, message formatting (stack buffer capped at 1024 bytes), text + JSON sinks, active sink registry.
- `src/logging/logging_internal.h` — Private declarations (scaffolded for future internals).
- `src/logging/logging_test.c` — Unit tests exercising level strings, ordering, and a capturing sink.
- `src/core/core.c` — Bootstraps the runtime log level from config (`core.log_level` / env `HB_LOG_LEVEL`) during `hbi_core_create`.
- `src/config/config.c` — Declares the `log.level` / `core.log_level` configuration keys consumed by core.
- `include/hummingbird/hummingbird.h` — Stable public ABI surface (logging symbols are internal `hbi_` prefix).

## Architecture and conventions
- **Module layering**: `logging` lives in layer 2 alongside peers like `config`, `profiler`, `device`. It depends only on `common` and `platform`; it must not depend on any other module or backends/frontends.
- **Record model**: Each emit constructs an `hbi_log_record` carrying level, nanosecond wall time, opaque thread id, source file/line, formatted message, and an array of `hbi_log_field` entries. Pointers are borrowed for the duration of the sink call only.
- **Sink contract**: A sink receives a fully-built record and renders it. Built-in sinks serialize writes with a single process-wide mutex created lazily behind a double-checked flag. Installing a custom sink bypasses this lock if the sink handles its own concurrency.
- **Level semantics**: Numeric ordering implements filtering; `PROFILING` is always enabled regardless of the severity floor because it represents an explicit opt-in stream rather than higher severity.
- **Compile-time vs runtime**: `HB_LOG_COMPILE_LEVEL` (default TRACE) strips code below the threshold at compile time; `hbi_log_set_level` adjusts the runtime filter atomically. The combination lets debug builds capture everything while release builds eliminate low-level logs entirely.
- **Integration point**: `hbi_core_create` reads `core.log_level` from the config subsystem and calls `hbi_log_set_level` early in initialization, before any other subsystem logs.

## Rules developers should follow
- Use the `HB_LOG_*` / `HB_LOG_FIELDS` macros from `logging/logging.h` rather than calling `hbi_log_emit` directly; they capture `__FILE__`/`__LINE__` and short-circuit on the compile-time floor.
- Keep messages under ~1 KB; longer ones are truncated with a `...` suffix — do not rely on full content being emitted.
- Attach structured data via `hbi_log_field` arrays when you need machine-parseable context (e.g., paths, sizes, IDs) instead of embedding it in the free-form message.
- Treat `PROFILING` as a separate diagnostic stream; use it sparingly and explicitly, since it is never filtered by the severity floor.
- Install a custom sink exactly once at program startup (before spawning threads) via `hbi_log_set_sink`; concurrent sink installation is undefined.
- Do not depend on `config`, `profiler`, frontends, tools, or backends from within `logging`; respect the documented allowed dependency set (`common`, `platform`).
- When raising verbosity in development, prefer setting the runtime level through the `core.log_level` config key (or `HB_LOG_LEVEL` env var) rather than redefining `HB_LOG_COMPILE_LEVEL`.