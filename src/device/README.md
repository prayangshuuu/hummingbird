# `device` module

Host capability report — CPU core counts, page/cacheline sizes, architecture
string, and the SIMD level the engine was compiled for — independent of any
backend. Answers "what can this host do?" for the thread pool, memory manager,
and (later) the CPU backend's tuning.

## Status

Implemented (Phase 5 runtime foundation). Provides a cached, copyable host
report built on the platform shim; no OS calls of its own. GPU enumeration is
intentionally out of scope — that is a backend concern.

## Public API

- `hbi_device_query(hbi_device_info *out)` — fill the cached host report.
- `hbi_device_logical_cores()` — clamped `>= 1` core count for pool sizing.
- `hbi_device_describe(buf, cap)` — one-line human summary (snprintf-style).
- `hbi_device_simd_level()` / `hbi_simd_level_str()` — compiled SIMD family.

## Layout

| File | Role |
|------|------|
| `device.h` | Core-public header (`hbi_device_*`). |
| `device_internal.h` | Private header. |
| `device.c` | Implementation (report cache, compile-time SIMD detection). |
| `device_test.c` | Unit tests (CTest target `device`). |
| `CMakeLists.txt` | Build target `hb_device`. |

## Allowed dependencies

This module may depend **only** on: `common`, `platform`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. In particular this
module must never depend on `backend` — the neutral hardware picture must be
available without any accelerator. See `docs/architecture/03-dependency-graph.md`.
