# `include/` — public headers (the stable ABI)

This is the **only** part of the tree external embedders include. Everything here
is the public contract of `libhummingbird`.

```c
#include <hummingbird/hummingbird.h>
```

## Tiers (ADR DD-014)

| Header | Prefix | Stability |
|--------|--------|-----------|
| `hummingbird/hummingbird.h` | `hb_` | **Stable.** Semver-guarded; never changed incompatibly without a major bump + ADR. |
| `hummingbird/hummingbird_experimental.h` | `hb_x_` | **Experimental.** Opt-in; may change or be removed without a major bump. |
| *(not here — lives in `src/`)* | `hbi_` | **Internal.** No stability guarantee; never installed. |

## Rules

- Nothing internal (`hbi_*`, module headers, private structs) may appear here.
- Only these headers are installed (`cmake --install`); the internal tree is not.
- Adding a symbol here is an API commitment — it goes through review and, for
  promotion out of experimental, an ADR.

See `docs/architecture/04-public-api.md` for the full strategy.
