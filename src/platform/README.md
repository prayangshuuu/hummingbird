# `platform` module

The **one place** OS, compiler, and architecture differences live (layer 1).
Every file I/O call, thread, mutex, condition variable, event, timer, CPU query,
aligned/huge-page allocation, and OS-error translation is behind this API. A
`#ifdef _WIN32` / `pread` / `mmap` / raw thread call anywhere under `src/` other
than `src/platform/` is a bug (see `docs/architecture/03-dependency-graph.md`).

## Status

Implemented (Phase 5, runtime foundation). No inference logic. Provides:

- **OS error** — `hbi_os_errno`, `hbi_os_strerror` (DD-022: platform turns the
  errno `common` recorded into text).
- **Memory** — `hbi_aligned_alloc`/`hbi_aligned_free`, `hbi_huge_alloc` (huge-page
  *hint* with transparent fallback), `hbi_page_size`.
- **CPU/topology** — `hbi_cpu_query` (logical/physical cores, page & cacheline
  size, arch string).
- **Time** — monotonic + wall clocks in ns, `hbi_sleep_ns`.
- **Threads** — `hbi_thread_create`/`join`, `hbi_thread_current_id`,
  `hbi_cpu_relax`.
- **Sync** — `hbi_mutex`, `hbi_cond`, and a portable manual-reset `hbi_event`
  latch built on mutex+cond.
- **Filesystem** — 64-bit-offset, binary-mode `hbi_file_*` including a
  thread-safe positional `hbi_file_pread` (the weight streamer depends on it),
  plus `hbi_path_exists`/`hbi_path_remove`.

Backends: Win32 and POSIX, selected by `#ifdef _WIN32` **inside `platform.c`**.

## Layout

| File | Role |
|------|------|
| `platform.h` | Core-public header (`hbi_platform_*`), the whole OS abstraction. |
| `platform_internal.h` | Private declarations (currently none beyond the header). |
| `platform.c` | Win32 + POSIX implementation, guarded by one `#ifdef`. |
| `platform_test.c` | Unit tests (CTest target `platform`). |
| `CMakeLists.txt` | Build target `hb_platform` (links pthreads on POSIX). |

## Allowed dependencies

This module may depend **only** on: `common`.

## Forbidden dependencies

Any module not listed above, and any cyclic dependency. Because this is the
portability boundary, nothing above layer 1 may be pulled in. See
`docs/architecture/03-dependency-graph.md` for the full rule set.

## Usage

```c
#include "platform/platform.h"

void *buf = hbi_aligned_alloc(64, 4096);
if (buf == NULL) { /* OS error already set; see hbi_os_strerror */ }
hbi_aligned_free(buf);

uint64_t t0 = hbi_time_monotonic_ns();
/* ... work ... */
uint64_t elapsed_ns = hbi_time_monotonic_ns() - t0;
```
