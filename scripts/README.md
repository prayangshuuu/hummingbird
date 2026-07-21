# `scripts/` — developer helper scripts

Small, dependency-light scripts used during development and CI. They are not
part of the shipped runtime and never run on the inference hot path.

| Script | Purpose |
|--------|---------|
| `scaffold_modules.sh` | Generates the skeleton file set (`<mod>.c/.h`, internal header, test, `CMakeLists.txt`, `README.md`) for a new `src/` module, matching the conventions in `docs/architecture/02-module-specifications.md`. Used to bootstrap the initial module tree; re-runnable for new modules. |

Conventions:
- POSIX `sh`/`bash` where possible (runs under Git Bash on Windows too).
- No required third-party dependencies.
- A script that mutates the tree must be idempotent and print what it did.
