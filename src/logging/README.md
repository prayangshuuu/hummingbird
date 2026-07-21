# `logging` module

Structured, leveled logging for human-readable diagnostics. This is deliberately
**separate from telemetry**: timings and counters are the profiler's job and go
to a machine-readable stream (PROJECT_CONTEXT §7, §3.14). Logging answers "what
happened"; the profiler answers "how long / how many".

## Status

`[BUILT]` — foundation implemented (Phase 5 / RFC-001). No inference logic.

## What it provides

- **Six levels**: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `PROFILING`
  (`hbi_log_level`). Severity ordering implements filtering; `PROFILING` is a
  distinct explicit stream, never dropped by the severity floor.
- **Runtime level control**: `hbi_log_set_level` / `hbi_log_get_level` /
  `hbi_log_enabled` (atomic, thread-safe). Default `INFO`.
- **Compile-time floor**: `HB_LOG_COMPILE_LEVEL` strips lower levels entirely so
  release builds pay nothing (not even argument evaluation) for `TRACE`/`DEBUG`.
- **Structured fields**: attach up to `HBI_LOG_MAX_FIELDS` key/value pairs to a
  record so a JSON sink can emit them without reparsing the message.
- **Pluggable sinks**: `hbi_log_set_sink`. Two built-ins, both thread-safe:
  `hbi_log_sink_text` (default, one line to stderr) and `hbi_log_sink_json`.

## Usage

```c
#include "logging/logging.h"

hbi_log_set_level(HBI_LOG_DEBUG);
HB_LOG_INFO("loaded %d shards", n);

hbi_log_field f[] = {{"path", path}, {"bytes", bytes_str}};
HB_LOG_FIELDS(HBI_LOG_WARN, f, 2, "slow read");
```

## Layout

| File | Role |
|------|------|
| `logging.h` | Public API: levels, record, sink, emit, `HB_LOG_*` macros. |
| `logging_internal.h` | Private declarations. |
| `logging.c` | Level filtering, message formatting, text + JSON sinks. |
| `logging_test.c` | Unit tests via a capturing sink (CTest target `logging`). |
| `CMakeLists.txt` | Build target `hb_logging`. |

## Allowed dependencies

`common`, `platform` (for the mutex, monotonic/wall time, and thread id).

## Forbidden dependencies

Any other module, and any cycle. In particular logging never depends on
`config` or `profiler` (they are peers in layer 2); a sideways edge there would
violate the layering. Frontends, tools, and backends must never be pulled in.
See `docs/architecture/03-dependency-graph.md`.
