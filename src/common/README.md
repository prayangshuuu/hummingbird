# `common` module

Foundational types, status codes, the unified error model, and small
header-only utilities shared by every module. This is **layer 0** — the single
lowest node in the dependency graph. It depends on nothing but the C standard
library and must never call an OS API (that is `platform`'s job).

## What it provides

| Area | API |
|------|-----|
| Status codes | `hbi_status` enum + `hbi_status_str()` (DD-011). |
| Error model | `hbi_error` thread-local record; `HBI_ERR_SET`/`HBI_ERR_SETF` to record at the call-site; `hbi_error_last`/`_clear`/`_format` to read (DD-011, DD-022). |
| Portability | `HBI_THREAD_LOCAL`, `HBI_LIKELY`/`HBI_UNLIKELY` — spelled portably without touching the OS. |
| Utilities | `HB_ARRAY_LEN`, `HB_UNUSED`, `HB_MIN`/`HB_MAX`, `hbi_is_pow2`, `hbi_align_up`. |

### Error model in one paragraph

A **status code** answers *what kind* of failure; the **thread-local error
record** answers *which one, where, and why*. Code that detects a failure sets
the record at the point of failure (`return HBI_ERR_SET(HBI_ERR_IO, os_errno,
"open failed")`), capturing `__FILE__/__LINE__/__func__` automatically. Callers
that want detail read it with `hbi_error_last()` or render a one-line summary
with `hbi_error_format()`. The record is per-thread, so concurrent operations
never clobber each other's diagnostics. The `os_errno` field preserves the true
platform error (cf. Colibrì #307); turning that number into text is
`platform`'s responsibility, not `common`'s.

## Usage

```c
#include "common/common.h"

hbi_status do_thing(const char *path) {
    if (path == NULL) {
        return HBI_ERR_SET(HBI_ERR_INVALID_ARG, 0, "path must not be NULL");
    }
    /* ... on OS failure, pass the real errno through ... */
    return HBI_ERR_SETF(HBI_ERR_IO, 2, "cannot open '%s'", path);
}

/* caller */
if (do_thing(p) != HBI_OK) {
    char buf[256];
    hbi_error_format(buf, sizeof buf);
    /* buf: "HBI_ERR_IO: cannot open '...' (os_errno=2) at foo.c:12 in do_thing" */
}
```

## Layout

| File | Role |
|------|------|
| `common.h` | Core-public header (`hbi_*`, `HBI_*`), included by every module. |
| `common_internal.h` | Private header — implementation details. |
| `common.c` | Status-code table + module self-test. |
| `error.c` | Thread-local error record implementation. |
| `common_test.c` | Unit test (CTest target `common`). |
| `CMakeLists.txt` | Build target `hb_common`. |

## Allowed dependencies

None. This module is foundational and must not depend on any other Hummingbird
module, nor on any OS API.

## Forbidden dependencies

Everything. A `#include` of any other `src/` module — or any `<windows.h>` /
`<unistd.h>` — from `common` is a layering bug. See
`docs/architecture/03-dependency-graph.md`.
