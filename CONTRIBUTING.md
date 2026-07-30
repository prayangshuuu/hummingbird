# Contributing to Hummingbird

Thank you for your interest in Hummingbird — a general-purpose C17 inference
runtime for large open-source LLMs.

---

## Getting Started

### Prerequisites

- CMake ≥ 3.20
- A C17-capable compiler: GCC ≥ 11, Clang ≥ 14, MSVC ≥ 19.29 (VS 2022), or
  Apple Clang ≥ 14
- Ninja (recommended; Make also works)

### Build and test (CPU-only, the default)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

A clean build with zero warnings is a hard gate — the project runs with
`-Werror` by default (`HB_WARNINGS_AS_ERRORS=ON`).

### Enable sanitizers (recommended for development)

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DHB_ENABLE_ASAN=ON -DHB_ENABLE_UBSAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

### Enable benchmarks

```sh
cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DHB_BUILD_BENCHMARKS=ON
cmake --build build-bench
# Run individual benchmarks from build-bench/benchmarks/
./build-bench/benchmarks/bench_kernel
./build-bench/benchmarks/bench_threadpool
```

### Enable optional backends

```sh
# CUDA (requires CUDA toolkit ≥ 11.8)
cmake -S . -B build-cuda -G Ninja -DHB_BACKEND_CUDA=ON
# Metal (requires macOS 13+ with Xcode toolchain)
cmake -S . -B build-metal -G Ninja -DHB_BACKEND_METAL=ON
```

---

## Project Layout

```
include/hummingbird/   Public stable ABI header (hb_ prefix)
src/                   Internal modules (hbi_ prefix), one subdirectory per module
  common/              Error records, status codes, shared macros
  platform/            OS abstraction (threads, files, clocks, aligned alloc)
  memory/              Allocator abstraction, arena, debug canaries
  tensor/              Shape, dtype, quant-meta, view/slice structs
  config/              Typed, validated configuration store
  kernel/              Op taxonomy, kernel registry, dispatch interface
  threadpool/          Work-stealing thread pool
  graph/               Computation graph builder and finalized graph
  scheduler/           Graph-to-execution-plan compiler (topological stages)
  stream/              Streaming engine: memory tiers, LRU cache, prefetch scheduler
  model/               Model loader pipeline: format detection, manifest, metadata
  quant/               Dequantization: fp16/bf16/mxfp4 → fp32
  adapter/             Model adapter framework + GPT-OSS adapter (RFC-015)
  kv/                  KV cache manager and context lifecycle (RFC-012)
  runtime/             Forward-pass orchestrator (under development)
backends/              Compute plug-ins registering against the kernel interface
  cpu/                 CPU reference backend (scalar, correctness-first)
  cuda/                CUDA backend (structure in place; kernels are stubs)
  metal/               Metal backend (registration stub; Apple Silicon future)
tests/
  support/             Header-only test harness (hbi_test.h)
  integration/         Tests that exercise multiple modules together
  e2e/                 End-to-end inference tests (placeholder; M1-M3 scope)
  property/            Property-based tests (placeholder; M4 scope)
benchmarks/            Micro-benchmarks for every major subsystem
```

---

## Module Dependency Rules

Modules are organized in strict layers (see `docs/architecture/03-dependency-graph.md`):

| Layer | Modules |
|---|---|
| 0 | `platform` |
| 1 | `common` |
| 2 | `config`, `memory` |
| 3 | `tensor` |
| 4 | `kernel`, `quant`, `backend` |
| 5 | `graph`, `threadpool` |
| 6 | `scheduler`, `stream` |
| 7 | `model` |
| 8 | `adapter`, `kv` |
| 9 | `runtime` |

A module may only depend on modules in **lower** layers. Lateral edges between
modules in the same layer are forbidden. The public `hummingbird.h` header
depends on nothing from `src/`.

---

## Writing a New Backend

A backend is a shared or static plugin that calls `hbi_kernel_register()` for
each op it implements and calls `hbi_backend_register()` to announce itself.

1. Create `backends/<name>/backend_<name>.c` and implement `hbi_backend`.
2. Create the kernel implementations and register them in `backend_<name>_init()`.
3. Register the backend entry in `backends/<name>/CMakeLists.txt` following the
   pattern in `backends/cpu/CMakeLists.txt`.
4. **Critical rule**: All registrations must happen at init time (single-threaded
   startup), before any worker thread calls `hbi_kernel_dispatch()`. The registry
   has no locking — violating this precondition is undefined behavior and will
   trigger an `HBI_ERR_STATE` error in debug builds.

---

## Writing a New Adapter

An adapter maps a model family's naming conventions and architecture to
Hummingbird's generic descriptor and graph.

1. Create `src/adapter/adapter_<name>.c` and implement `hbi_model_adapter`:
   - `validate_metadata()` — check required keys are present
   - `build_descriptor()` — parse metadata into `hbi_model_descriptor`
   - `register_tensors()` — verify the manifest contains required tensors
   - `build_graph()` — emit graph nodes for the architecture
   - `create_context()` / `destroy_context()` — per-context state
   - `get_capabilities()`, `get_statistics()`, `shutdown()`
2. Expose a `hbi_adapter_<name>_register()` function.
3. Write a dedicated `adapter_<name>_test.c` covering:
   - Metadata validation (valid + missing keys + wrong values)
   - Descriptor parsing (check all numeric fields)
   - Tensor registration (happy path + missing required tensor)
   - Graph construction (node count, value count, execution order)
   - Context lifecycle
4. See `src/adapter/adapter_mock.c` (the mock) and `src/adapter/adapter_gpt_oss.c`
   for reference implementations.

---

## Writing a New Model Format Handler

A format handler plugs into the model loader pipeline via `hbi_format_handler`.

1. Implement `detect()` — probe the first 16 bytes for a magic signature.
2. Implement `parse_metadata()` — populate `hbi_model_manifest` and
   `hbi_model_metadata` without loading tensor data.
3. Implement `read_tensor_data()` — copy bytes for a single tensor entry.
4. Call `hbi_format_handler_register()` at init time.
5. Write a round-trip test: write a minimal binary fixture, load it, verify
   every tensor entry and metadata key, and read tensor data back.
6. Write a corrupt-input test: malformed magic, out-of-bounds offsets, unknown
   dtype — all must return a non-OK status, never crash.

See `src/model/model_safetensors.c` for the Safetensors reference implementation.

---

## Test Requirements

Every new module or feature needs tests. The minimum bar is:

| Test type | Required? |
|---|---|
| Unit test (happy path) | ✅ Always |
| Unit test (NULL / invalid args) | ✅ Always |
| Unit test (edge cases) | ✅ Always |
| Negative / error path | ✅ Always |
| Thread safety / concurrency | ✅ If the module has documented thread contracts |
| Cross-module integration | ✅ For high-level APIs |
| Benchmark | ✅ For performance-sensitive modules |

The test harness is `tests/support/hbi_test.h`. Use `HBI_TEST_BEGIN` /
`HBI_RUN` / `HBI_TEST_END` for new tests. Older modules use `ASSERT` macros
directly — match the style of the file you are editing.

### Adding a test to CMake

```cmake
# In src/<module>/CMakeLists.txt
add_executable(hb_<module>_test <module>_test.c)
target_link_libraries(hb_<module>_test PRIVATE hb_<module> hb_test_support)
add_test(NAME hb_<module>_test COMMAND hb_<module>_test)
```

---

## Coding Style

- **Language**: C17. No compiler extensions (`CMAKE_C_EXTENSIONS OFF`).
- **Naming**:
  - Public stable API: `hb_` prefix (e.g., `hb_version()`)
  - Internal cross-module API: `hbi_` prefix (e.g., `hbi_kernel_dispatch()`)
  - Internal-to-file helpers: no prefix, `static`
  - Macros: `HBI_` prefix (internal) or `HB_` (public/shared)
- **Formatting**: enforced by `clang-format` (run `clang-format -i` before committing)
- **Errors**: every public function returns `hbi_status`. Never call `exit()`,
  `abort()`, or `assert()` in library code (DD-011/DD-019). Use `HBI_ERR_SET`
  to attach a diagnostic to the thread-local error record.
- **Memory**: use `hbi_alloc` / `hbi_free` (or the arena) rather than `malloc`
  directly. Tag every allocation with an appropriate `hbi_mem_tag`.
- **Thread safety**: document the thread-safety contract in every module header.
  The standard contract is: "populate at init time (single-threaded), then
  read-only from worker threads."

---

## Pull Request Checklist

Before opening a PR:

- [ ] `cmake --build build` succeeds (Debug and Release)
- [ ] `ctest --test-dir build --output-on-failure` passes
- [ ] `ctest --test-dir build-asan --output-on-failure` passes (ASan+UBSan build)
- [ ] New code has unit tests
- [ ] Public API changes are documented in the relevant header
- [ ] No new compiler warnings
- [ ] Commit messages are clear and reference the gap/RFC/ADR being addressed

---

## Hardware-Gated Items

Some features cannot be tested without specific hardware:

| Feature | Hardware required |
|---|---|
| CUDA backend kernels | NVIDIA GPU (driver + CUDA toolkit ≥ 11.8) |
| Metal backend | macOS 13+ with Apple Silicon |
| Huge-page allocation | Linux with `CAP_SYS_ADMIN` or `vm.nr_hugepages > 0` |

If you are contributing to a hardware-gated path:
1. Document the setup steps in `docs/backends/<name>/SETUP.md`.
2. Gate the relevant test with `REQUIRE_HARDWARE_<NAME>` in CMake.
3. Mark the CI step with `runs-on: [self-hosted, <tag>]`.

---

## Reporting Bugs

Open a GitHub issue with:
- Hummingbird version (`hb_version_string()`)
- Platform and compiler version
- Minimal reproduction case
- Expected vs. actual behavior
