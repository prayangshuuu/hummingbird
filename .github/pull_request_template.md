# Pull Request

## Summary

<!-- What does this PR do, and why? Link the issue it closes: "Closes #123". -->

## Type of change

- [ ] Bug fix (non-breaking)
- [ ] New feature (non-breaking)
- [ ] Breaking change (requires a major version bump + an ADR)
- [ ] Documentation only
- [ ] Build / CI / tooling
- [ ] Refactor (no behavior change)

## Checklist

- [ ] I read `CONTRIBUTING.md` and this change respects the module dependency
      rules (`docs/architecture/03-dependency-graph.md`).
- [ ] The tree builds with warnings-as-errors (`cmake --preset debug && cmake --build build`).
- [ ] `ctest --preset debug` passes locally.
- [ ] `clang-format` clean; no new `clang-tidy` regressions.
- [ ] New/changed behavior is covered by tests (unit next to the module; e2e/property where relevant).
- [ ] Public API changes are reflected in `include/hummingbird/*.h` and documented.
- [ ] **`.claude/PROJECT_CONTEXT.md` is updated in this same PR** (ADR log, roadmap,
      benchmarks, known issues — whichever applies). This is mandatory; see the repo policy.
- [ ] If this is an architectural decision or changes one, a new `DD-0xx` ADR entry is added.

## Correctness / performance notes

<!-- Did you verify token-exactness where relevant? Any benchmark numbers?
     Machine + commit for any perf claim (see PROJECT_CONTEXT §9). -->

## Breaking changes / migration

<!-- If this breaks the public API or on-disk format, describe the migration path. -->
