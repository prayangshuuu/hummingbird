# `third_party/` — vendored external dependencies

Hummingbird is **dependency-light by design** (Core Principle 4: "one artifact,
few dependencies"). Nothing here is required for a default CPU-only build today —
the tree is empty on purpose.

## What belongs here

Only code we did not write but must ship or vendor, each in its own subdirectory:

```
third_party/
  <name>/
    <upstream sources>
    README.md      ← what it is, why we need it, exact upstream version/commit
    LICENSE        ← the dependency's own license (kept verbatim)
```

## Rules

- **Justify every entry.** A new `third_party/` dependency needs an ADR
  (`.claude/PROJECT_CONTEXT.md` §4) explaining why it cannot be avoided. The bar is
  high: no required BLAS, no required Python at runtime (see Non-Goals).
- **Pin exactly.** Record the upstream tag or commit hash. Never track a moving
  branch.
- **License compatibility.** Must be compatible with Hummingbird's license
  (Apache-2.0). Record the SPDX identifier in the sub-README.
- **No edits without a patch record.** If we must patch vendored code, keep the
  change in a `patches/` file next to it so upgrades are reproducible.
- **Prefer build-time/optional.** Test-only or tooling dependencies should be
  fetched via CMake `FetchContent` guarded by an option, not committed here,
  unless offline/hermetic builds require vendoring.

## Current contents

_None._ The engine core, CPU backend, and both frontends build against the C
standard library and OS APIs (through `src/platform/`) alone.
