# Contributing to Hummingbird

Thanks for your interest in Hummingbird.

## Workflow

1. Fork the repository and create a topic branch off `main`.
2. Build and run the test suite:
   ```sh
   cmake --preset dev
   cmake --build --preset dev
   ctest --preset dev
   ```
3. Keep changes scoped to a single module where possible, and match the
   existing code style (`clang-format` is enforced in CI).
4. Ensure the full test suite passes, then open a pull request against `main`
   with a clear description of what changed and why.

## Guidelines

- New code needs tests. Every module carries its own unit test.
- Public API changes belong in `include/hummingbird/` and must stay ABI-stable.
- CI (build, tests, formatting) must be green before a PR is merged.
