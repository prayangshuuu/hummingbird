# `cmake/` — build-system helper modules

Reusable CMake logic `include()`d by the root `CMakeLists.txt`. Keeping it here
keeps per-module `CMakeLists.txt` files tiny and consistent.

| File | Purpose |
|------|---------|
| `modules/HBCompilerOptions.cmake` | Defines `hb_apply_common(target)` — the shared warning set (`-Wall -Wextra -Wpedantic -Wconversion …`), `-Werror` policy, and C17 enforcement applied to every target. |
| `modules/HBSanitizers.cmake` | Wires `HB_ENABLE_ASAN` / `HB_ENABLE_UBSAN` into the common flags. |

Every target — library, test, frontend, backend, example — calls
`hb_apply_common()` so warnings and standards are enforced uniformly. A module
that skips it is a review red flag.

See `docs/architecture/05-build-system.md`.
