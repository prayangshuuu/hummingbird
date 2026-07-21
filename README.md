<p align="center">
  <img src="img.png" alt="Hummingbird" width="100%"/>
</p>

ughhghhh<p align="center">
  <img src="https://img.shields.io/badge/language-C17-blue" alt="C17"/>
  <img src="https://img.shields.io/badge/build-CMake%20%E2%89%A5%203.20-orange" alt="CMake"/>
  <img src="https://img.shields.io/badge/license-Apache--2.0-green" alt="Apache-2.0"/>
  <img src="https://img.shields.io/badge/status-early%20foundation-yellow" alt="Status"/>
</p>

<p align="center">
  A zero dependency C17 runtime for running massive open source LLMs,<br/>
  both dense and Mixture of Experts, on consumer hardware.
</p>

> **Early foundation.** The engine does not run inference yet. This tree is a compiling, tested scaffold: a modular C17 core, a stable public C ABI, a pluggable backend ABI, and the full build, test, and CI setup. Only version and status introspection are implemented today. Real inference lands one module at a time along the roadmap below.

## Contents

[Why Hummingbird](#why-hummingbird) &nbsp;·&nbsp; [How it works](#how-it-works) &nbsp;·&nbsp; [System architecture](#system-architecture) &nbsp;·&nbsp; [The memory hierarchy](#the-memory-hierarchy) &nbsp;·&nbsp; [Module reference](#module-reference) &nbsp;·&nbsp; [Backends and the compute ABI](#backends-and-the-compute-abi) &nbsp;·&nbsp; [Quantization](#quantization) &nbsp;·&nbsp; [Threading and concurrency](#threading-and-concurrency) &nbsp;·&nbsp; [Embedding Hummingbird](#embedding-hummingbird) &nbsp;·&nbsp; [Testing strategy](#testing-strategy) &nbsp;·&nbsp; [Public API](#public-api) &nbsp;·&nbsp; [Getting started](#getting-started) &nbsp;·&nbsp; [Build options](#build-options) &nbsp;·&nbsp; [Repository layout](#repository-layout) &nbsp;·&nbsp; [Continuous integration](#continuous-integration) &nbsp;·&nbsp; [Contributing](#contributing) &nbsp;·&nbsp; [Versioning and releases](#versioning-and-releases) &nbsp;·&nbsp; [Design rationale](#design-rationale) &nbsp;·&nbsp; [Security and reporting](#security-and-reporting) &nbsp;·&nbsp; [Roadmap](#roadmap) &nbsp;·&nbsp; [FAQ](#frequently-asked-questions) &nbsp;·&nbsp; [Glossary](#glossary) &nbsp;·&nbsp; [Acknowledgements](#acknowledgements) &nbsp;·&nbsp; [License](#license)

## Why Hummingbird

Frontier open weight models have grown past the point where they fit in the memory of an ordinary machine. A model with hundreds of billions of parameters needs far more RAM than a laptop or a single consumer GPU can offer, and the obvious answer, buying more hardware, is out of reach for most people who want to run these models locally.

There is a structural detail that changes the picture. Many of the largest open models are Mixture of Experts models. In such a model each layer holds a large pool of expert subnetworks, but a small router picks only a handful of them for any given token. The rest of the experts sit idle for that token. Across a whole forward pass the model touches only a fraction of its total weights, and which fraction it touches shifts from token to token.

That observation is the whole premise of Hummingbird. If most of the weights are idle at any instant, the model does not need to fit in memory. It only needs the currently active weights to be in memory, with the rest reachable quickly when the router calls for them. The problem shifts from fitting the model to placing the model across the storage you already have.

Hummingbird is a from scratch inference runtime built around tha1t idea. It unifies SSD, RAM, and VRAM into a single managed hierarchy, keeps the always needed weights resident, streams the large pool of expert weights from disk on demand, and caches what it has recently used so the engine grows faster the longer it runs. The design lets a machine with a modest amount of RAM run a model whose total size dwarfs that RAM, and it does so in portable C with no third party runtime dependencies.

The one rule that everything else bends around is that placement decides speed and never correctness. Where a weight physically lives, whether it came from a hot cache or a cold disk read, and whether the scheduler guessed right about what to prefetch are all performance concerns. None of them may change the tokens the model produces. Running a model larger than your RAM must give the same answer as running it fully resident, bit for bit, or the trick is not worth doing.

## How it works

A single decode step moves through the engine in a predictable sequence. Understanding that sequence is the fastest way to understand the codebase, because the modules map onto it almost one to one.

**1. Load and describe the model.** A model is turned into a descriptor plus a forward graph. The descriptor records geometry such as layer count, hidden sizes, head configuration, and the quantization class of every tensor. The forward graph is a skeleton of typed operation nodes, for example RMS normalization, attention, a Mixture of Experts block, a plain feed forward block, and residual joins. Nothing about a specific architecture is baked into the core. Adding a new model means providing new data, not editing the engine.

**2. Plan placement.** Every tensor in the model carries a weight class that says how it should be treated. Weights that are needed for every token, such as attention projections, embeddings, and the router, are marked resident and kept in fast memory. The large pool of routed expert weights is marked streamable and assigned to disk with a cache in front of it. Weights that are numerically sensitive can be pinned to a higher precision. From these classes and the available memory the manager computes a RAM budget and a placement plan.

**3. Run the forward loop.** The runtime walks the forward graph layer by layer. For each operation node it dispatches to the matching typed operation module, which runs on the active compute backend. Dense layers execute directly from resident memory. When the loop reaches a Mixture of Experts block, the router runs first and names the experts this token needs.

**4. Stream and cache the experts.** The needed expert weights are fetched through the streaming layer. Because experts are laid out adjacently on disk, a whole streamable unit is read in a single coalesced I/O and exposed to the kernels as a zero copy view rather than being copied again. Fetched weights pass through a cache hierarchy: a pinned tier that is never evicted, an LRU tier sized to fill the RAM budget, and disk behind both. A learning cache remembers usage patterns so that frequently paired experts stay warm.

**5. Overlap I/O with compute.** While the current layer computes, the scheduler prefetches the weights the next steps are likely to need, so disk latency hides behind arithmetic that is already happening. Prefetch and any speculative execution are strictly optimizations. If a guess is wrong the engine simply does the work the normal way, and the output is identical.

**6. Compute, sample, repeat.** The tensor runtime performs the quantized matrix multiplications that dominate the cost, the runtime produces logits, the per session context applies sampling to choose the next token, and the loop repeats until the sequence is complete. Frontends such as the CLI and the server sit on top of this loop and translate it into a command line tool or an HTTP API.

The result is a pipeline in which the disk is treated as the bottom of a memory hierarchy rather than as a separate world, and in which every clever thing the engine does to be fast is fenced off from the part that must stay exact.

## System architecture

The engine is organized as strict layers. A module may depend only on modules below it, never sideways within its own layer and never upward. This rule is enforced in review and keeps the dependency graph acyclic, which in turn keeps the build fast, the tests isolated, and the reasoning local.

```
   ┌──────────────────────────────────────────────────────────────┐
   │  Frontends            hb CLI        hb-server (OpenAI style)   │
   │  (may exit())         command line   HTTP serving              │
   └───────────────────────────────┬──────────────────────────────┘
                                    │  libhummingbird  (stable C ABI, hb_*)
   ┌───────────────────────────────┴──────────────────────────────┐
   │  Orchestration        runtime · context · core                │
   ├────────────────────────────────────────────────────────────── ┤
   │  Execution            scheduler · executor · model · graph      │
   ├────────────────────────────────────────────────────────────── ┤
   │  Data movement        stream · kv · backend · tokenizer         │
   ├────────────────────────────────────────────────────────────── ┤
   │  Numerics             tensor · quant                            │
   ├────────────────────────────────────────────────────────────── ┤
   │  Resources            memory · device · threadpool              │
   ├────────────────────────────────────────────────────────────── ┤
   │  Foundation           common · platform · logging · profiler ·  │
   │                       config                                    │
   └────────────────────────────────────────────────────────────── ┘
             │                                        │
             ▼ model files (safetensors)              ▼ backends: CPU · CUDA · Metal
                                                        one memory tier: VRAM / RAM / NVMe
```

**Frontends.** The command line tool `hb` and the OpenAI style server `hb-server` are the only binaries permitted to terminate the process. They are thin translators over the public library and hold no engine logic of their own.

**Orchestration.** The runtime owns the forward loop, sequences the layers, drives the scheduler, and produces logits. The core is the runtime context that owns the lifecycle of every foundation subsystem for one engine instance. The per session context holds KV state, sampling state, and run mode so that several independent sessions can share one loaded model.

**Execution.** The scheduler overlaps I/O with compute and issues prefetches. The executor walks a forward graph and dispatches each node to its typed operation module on the active backend. The model layer holds the descriptor, the forward graph, the tokenizer reference, and the quantization classes. The graph layer defines the node types and the registry of typed operation modules.

**Data movement.** The stream module turns on disk contiguity into single coalesced reads exposed as zero copy views. The KV module abstracts the key value cache across multi head, grouped query, and compressed latent layouts, and supports save and restore. The backend module is the stable `extern "C"` boundary that compute backends implement, with automatic per tensor fallback to the CPU. The tokenizer module adapts byte pair encoding, unigram, and similar schemes behind one encode and decode interface.

**Numerics.** The tensor module is the quantized tensor representation and the CPU matmul dispatch, covering both exact floating point and integer dot product regimes. The quant module is the registry of quantization formats, defining the pack and unpack contracts and format detection.

**Resources.** The memory module is the foundation allocator layer, offering a generic allocator interface on which the higher tiers are later built. The device module reports host capability such as core counts, page and cache line sizes, and architecture. The threadpool module is a reusable fixed size worker pool for generic parallel work.

**Foundation.** The common module holds the shared types, the status codes, and the unified error model. The platform module is the single place where operating system, compiler, and architecture differences live. Logging provides structured leveled diagnostics for humans, the profiler provides machine readable timing and counters, and config provides typed validated configuration layered as defaults, then file, then environment, then programmatic override.

## The memory hierarchy

The defining feature of Hummingbird is that SSD, RAM, and VRAM are treated as one hierarchy rather than three separate boxes, and that placement across that hierarchy is driven by how weights are actually used.

```
   ┌─────────────┐   promote hot     ┌─────────────┐   promote hot   ┌─────────────┐
   │    NVMe     │  ───────────────▶ │     RAM     │ ──────────────▶ │    VRAM     │
   │  (SSD)      │                    │  LRU cache  │                  │  resident   │
   │  cold pool  │  ◀─────────────── │  auto sized │ ◀────────────── │  hot compute │
   │  of experts │   evict cold       │             │   evict cold    │             │
   └─────────────┘                    └─────────────┘                  └─────────────┘
         ▲                                   ▲                                ▲
   coalesced reads,                    pinned tier never                resident weights
   zero copy views                     evicted, sized to                always in fastest
                                       fill the RAM budget               memory
```

Three ideas do the work.

**Weight classes.** Placement is not guessed at runtime from scratch. Each tensor is classified up front. Resident weights are the ones every token needs and they stay in the fastest memory available. Streamable weights are the large pool of experts that live on disk and move up the hierarchy on demand. Sensitive weights can be pinned to a higher precision so that placement never trades away numerical quality.

**A learning cache.** In front of the disk sits a cache with a pinned tier that is never evicted, an LRU tier that is automatically sized to consume the RAM budget without overflowing it, and the disk beneath. The cache records which experts get used and how they cluster, so repeated workloads keep the right weights warm and the engine measurably speeds up the more it is used.

**Coalesced, zero copy streaming.** The single highest leverage streaming decision is layout. Because a streamable unit of expert weights is stored adjacently on disk, the engine reads the whole unit in one large sequential I/O instead of many scattered ones, then hands the kernels a view directly into that buffer with no extra copy. Large sequential reads are exactly what SSDs are good at, which is what makes streaming from disk fast enough to be practical.

Around all of this the memory manager computes a RAM budget from the machine's real capacity and guards against running out of memory, so that unifying the tiers never turns into silently overcommitting them.

## Module reference

The engine core is twenty two modules under `src/`, each with a public header, a private header, an implementation, and its own unit test. They are listed here from the foundation upward.

| Module | Responsibility |
|--------|----------------|
| `common` | Foundational types, status codes, and the unified error model shared everywhere. |
| `platform` | The single place OS, compiler, and architecture differences live. |
| `logging` | Structured, leveled, human readable diagnostics. |
| `profiler` | Lightweight machine readable timing, counter, and event instrumentation. |
| `config` | Typed, validated configuration: defaults, then file, then environment, then programmatic. |
| `threadpool` | A reusable, fixed size worker pool for generic parallel work. |
| `device` | Host capability report: core counts, page and cache line sizes, architecture. |
| `memory` | The foundation allocator layer and its generic allocator interface. |
| `quant` | Quantization format registry: pack and unpack contracts and format detection. |
| `tensor` | Quantized tensor representation and CPU matmul dispatch, exact and integer dot regimes. |
| `kernel` | Backend agnostic op taxonomy, kernel descriptor, and dispatch registry; backends register their kernels here. |
| `stream` | Weight streaming: contiguity to a single coalesced read to zero copy views. |
| `kv` | Key value cache layouts for multi head, grouped query, and compressed latent attention, with save and restore. |
| `backend` | The stable `extern "C"` backend ABI with automatic per tensor CPU fallback. |
| `tokenizer` | Tokenizer adapter for byte pair encoding, unigram, and similar schemes. |
| `graph` | Forward graph node types and the typed operation module registry. |
| `model` | The model adapter: descriptor, forward graph, tokenizer reference, and quant classes. |
| `executor` | Walks a forward graph and dispatches each node to its typed module on the active backend. |
| `scheduler` | Overlaps I/O with compute and prefetches experts; speculative actions never change output. |
| `core` | The runtime context: lifecycle and ownership of the foundation subsystems. |
| `context` | Per session decode context: KV state, sampling state, run mode. |
| `runtime` | The orchestrator: owns the forward loop, sequences layers, drives the scheduler, produces logits. |

Adding a new model architecture is meant to be additive. It provides a descriptor and a forward graph made of existing node types, and it does not require touching the modules above.

### Foundation modules in depth

The foundation layer exists so that everything above it can be written without repeating platform checks, error plumbing, or configuration parsing.

The `common` module defines the status enumeration, the error reporting helpers, and the small shared value types that appear in nearly every signature. Because it sits at the very bottom, it depends on nothing else in the tree, which is what allows every other module to depend on it freely.

The `platform` module is the only module allowed to contain operating system specific branches. File access, memory mapping, page size queries, and similar system calls are wrapped here behind a portable interface, so a bug that only appears on one operating system has exactly one place to live. Any system dependent code found outside this module is treated as a defect.

The `logging` and `profiler` modules answer two different questions and are deliberately kept apart. Logging answers what happened, as leveled human readable text. The profiler answers how long it took and how often, as machine readable timing and counter streams. Keeping the two separate means diagnostics never pollute the measurement stream and measurements never get lost in prose.

The `config` module resolves configuration from four ordered sources. Built in defaults come first, a configuration file overrides them, environment variables override the file, and explicit programmatic settings override everything. Values are validated as they are resolved, so an invalid configuration fails early with a clear status rather than surfacing as a strange result much later.

### Resource and numeric modules in depth

The `memory` module provides a generic allocator interface rather than calling the system allocator directly throughout the codebase. Routing allocations through one interface is what later makes budgeting, arena allocation, and alignment guarantees possible without rewriting callers.

The `device` module reports what the host actually is, including the number of cores, the page and cache line sizes, and the architecture. Higher layers read this report to size the thread pool, choose alignment, and pick the best available kernel path.

The `threadpool` module is a fixed size pool of worker threads that executes generic units of work. Sizing it once from the device report avoids the cost of creating and destroying threads on the hot path.

The `quant` module is the registry of quantization formats. Each format defines how packed values are laid out, how they unpack into computable numbers, and how the format is detected. Keeping this in a registry means new formats are added as data rather than as scattered special cases.

The `tensor` module is the quantized tensor representation together with the CPU matmul dispatch. It selects between an exact floating point path and an integer dot product path depending on the tensors involved, and it is the module that the compute heavy kernels ultimately run through.

### Data movement and execution modules in depth

The `stream` module enforces the streaming invariant that makes disk backed inference practical: contiguity leads to a single coalesced read, which leads to a zero copy view. It is the module most directly responsible for turning slow scattered disk access into the fast sequential access that SSDs handle well.

The `kv` module abstracts the key value cache across the attention layouts that modern models use, including multi head attention, grouped query attention, and compressed latent attention, and it can save and restore that state so sessions can be paused and resumed.

The `backend` module defines the stable `extern "C"` boundary that a compute backend implements. Its most important guarantee is per tensor fallback: if a backend cannot handle a given operation, the engine falls back to the CPU for that operation rather than failing the whole run.

The `tokenizer` module adapts different tokenization schemes, such as byte pair encoding and unigram models, behind one encode and decode interface, so the runtime does not care which scheme a given model uses.

The `graph`, `model`, `executor`, `scheduler`, `core`, `context`, and `runtime` modules together form the execution and orchestration layers described in the architecture section. The graph defines the node types, the model binds those nodes into a concrete architecture, the executor dispatches them, the scheduler keeps the disk hidden behind compute, and the runtime drives the whole forward loop while the core and context own lifecycle and per session state.

## Backends and the compute ABI

Compute backends are plug ins behind a stable C boundary rather than compile time forks scattered through the engine. A backend implements the `extern "C"` interface defined by the `backend` module, and the engine dispatches operations to whichever backend is active.

The reference CPU backend is always built and is the correctness baseline. Its kernels are written to be clear and portable, with compile time selection of the best available SIMD path and a scalar fallback for platforms that lack it. Every other backend is measured against this one.

Optional GPU backends for CUDA and Metal are compiled in only when their toolchains are present and the corresponding build option is enabled. The defining rule of the backend boundary is that acceleration is never allowed to become a correctness risk. If a backend cannot perform a given operation, or if a backend call fails, the engine falls back to the CPU for that tensor. A GPU makes the engine faster, never less correct, and a model produces the same tokens regardless of which backend served a given operation.

## Quantization

Large models are almost always stored and run in a quantized form, where weights are represented with fewer bits than a full floating point number. Quantization is what lets a model of a given parameter count occupy far less storage and memory, and it is central to running these models on consumer hardware.

Hummingbird treats quantization formats as data. The `quant` module holds a registry in which each format declares how its packed bytes are laid out, how they unpack into values the kernels can compute with, and how the format is recognized. The `tensor` module then dispatches matrix multiplication to either an exact floating point path or an integer dot product path, choosing based on the formats of the tensors involved.

Two rules keep quantization safe. First, a tensor that is numerically sensitive can be pinned to a higher precision through its weight class, so the placement and streaming machinery never silently degrades quality to save space. Second, quantized kernels are validated against a scalar reference implementation, so a fast packed kernel must produce the same result as the obvious slow one before it is trusted.

## Threading and concurrency

Concurrency is deliberately concentrated rather than sprinkled through the codebase. The `threadpool` module owns worker threads, and the `platform` module owns the primitives that make concurrency portable. Modules that need parallel work submit it to the pool instead of spawning threads of their own, which keeps thread creation off the hot path and keeps the number of live threads predictable and sized to the host.

The per session `context` module is what allows several independent decode sessions to share a single loaded model. The large resident and streamed weights are read only during inference and can be shared safely, while the mutable per session state, such as the key value cache and the sampling state, lives in the context. Anything that touches shared mutable state must document its thread safety contract, and that requirement is part of the review checklist.

## Embedding Hummingbird

Because the engine is a library with a stable C ABI, it can be embedded into another program directly. The only header an embedder includes is the public one, and linking against `libhummingbird` is all that is required. The example below reflects the surface available in the current bootstrap.

```c
#include <hummingbird/hummingbird.h>
#include <stdio.h>

int main(void) {
    /* Query the runtime version of the library we linked against. */
    printf("hummingbird %s\n", hb_version_string());

    /* Status codes turn into stable, human readable strings. */
    hb_status st = HB_ERR_NOT_IMPLEMENTED;
    printf("example status: %s\n", hb_status_string(st));

    return 0;
}
```

As later milestones land, the same pattern extends to loading a model, creating a decode context, and running the forward loop, all through `hb_` prefixed functions that follow the same error model and stability guarantee shown here.

## Testing strategy

Testing runs at several levels so that both small units and whole flows are covered. Every module under `src/` ships with its own unit test that exercises that module in isolation. Above the modules, the `tests/` tree holds cross module tests split into integration tests that check modules working together, property tests that assert invariants hold across many generated inputs, and end to end tests that will exercise a whole flow once inference lands.

Two testing rules matter most. New code must come with tests, and code that touches memory must be clean under the sanitizer build, which is exactly why continuous integration runs a dedicated AddressSanitizer and UndefinedBehaviorSanitizer job. Together with the correctness first principle, this is how the project intends to keep a fast engine from ever becoming a wrong one.

## Public API

External code integrates through exactly one header:

```c
#include <hummingbird/hummingbird.h>
```

Every symbol in that header is prefixed `hb_` and is semantically versioned. It will not be removed or changed incompatibly without a major version bump. Experimental, opt in symbols live in a separate `hummingbird_experimental.h` header under the `hb_x_` prefix and carry no stability guarantee, and internal engine symbols use the `hbi_` prefix and are never installed.

In the current bootstrap the public surface covers version and status introspection. Model loading, contexts, and decoding are declared for later milestones.

```c
/* Runtime library version, for example "0.0.0". Never NULL. */
const char *hb_version_string(void);

/* Packed version integer: MAJOR * 10000 + MINOR * 100 + PATCH. */
int hb_version(void);

/* Non localized description of a status code. Never NULL. */
const char *hb_status_string(hb_status status);
```

The error model is a single stable enumeration. Values are assigned once and never change, and new codes are only appended, so the numeric contract stays stable across versions.

| Status | Meaning |
|--------|---------|
| `HB_OK` | Success. |
| `HB_ERR_UNKNOWN` | Unclassified failure. |
| `HB_ERR_INVALID_ARG` | The caller passed an invalid argument. |
| `HB_ERR_NO_MEMORY` | An allocation failed. |
| `HB_ERR_IO` | A filesystem or device I/O error occurred. |
| `HB_ERR_NOT_FOUND` | The requested entity does not exist. |
| `HB_ERR_UNSUPPORTED` | The operation is not supported in this build or configuration. |
| `HB_ERR_CORRUPT` | The data was malformed, for example a bad container or a failed bounds check. |
| `HB_ERR_NOT_IMPLEMENTED` | Declared but not yet built in this scaffold. |

Library code always reports failure by returning an `hb_status`. It never calls `exit()`, and it preserves the underlying operating system error so callers can diagnose the true cause. Only the frontends may terminate the process.

## Getting started

You need a C17 compiler, which can be GCC, Clang, or MSVC, along with CMake version 3.20 or newer and the Ninja generator, which is recommended. There are no third party runtime dependencies, so no BLAS and no Python are required to build or run the engine.

```sh
git clone https://github.com/prayangshuuu/hummingbird.git
cd hummingbird

cmake --preset dev            # configure a Debug build with tests enabled
cmake --build --preset dev    # build the engine, frontends, and tools
ctest --preset dev            # run the full test suite
```

Once the build finishes, run the scaffold binaries to confirm everything links and executes:

```sh
./build/frontends/cli/hb --version      # print the hb CLI version
./build/examples/example_version        # a small library embedding example
```

Two further presets build the same way, with `dev` replaced by the preset name. The `asan` preset adds AddressSanitizer and UndefinedBehaviorSanitizer for catching memory and undefined behavior bugs, and the `release` preset produces an optimized build with benchmarks enabled. If you prefer not to use presets, a plain configure and build works too:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Build options

Set any option at configure time with `-D<option>=ON` or `-D<option>=OFF`.

| Option | Default | Description |
|--------|:-------:|-------------|
| `HB_BUILD_TESTS` | ON | Build unit and integration tests and register them with CTest. |
| `HB_BUILD_FRONTENDS` | ON | Build the `hb` CLI and `hb-server`. |
| `HB_BUILD_TOOLS` | ON | Build the offline tooling. |
| `HB_BUILD_EXAMPLES` | ON | Build the library embedding examples. |
| `HB_BUILD_BENCHMARKS` | OFF | Build the performance harnesses. |
| `HB_BACKEND_CPU` | ON | The reference CPU backend, always recommended. |
| `HB_BACKEND_CUDA` | OFF | The CUDA backend, which requires the CUDA toolkit. |
| `HB_BACKEND_METAL` | OFF | The Metal backend for Apple platforms. |
| `HB_ENABLE_ASAN` / `HB_ENABLE_UBSAN` | OFF | Address and undefined behavior sanitizer builds. |
| `HB_WARNINGS_AS_ERRORS` | ON | Treat compiler warnings as errors. |

## Repository layout

```
include/hummingbird/   Public C ABI, the only headers embedders include
src/                   Engine core, modular with one unit test per module
backends/              Compute backends: cpu · cuda · metal
frontends/             hb CLI and hb-server, the only binaries that may exit()
tools/                 Offline tooling: converter, oracle, ablation
tests/                 Cross module tests: integration · property · e2e
examples/              Small programs that embed the public library
benchmarks/            Performance harnesses
cmake/                 Build helper modules
.github/               CI workflows and issue and PR templates
```

Every module under `src/` follows the same shape: a public header named `<mod>.h`, a private header named `<mod>_internal.h`, an implementation named `<mod>.c`, and a unit test. Symbols split by visibility. The `hb_` prefix marks the public, ABI stable surface, and the `hbi_` prefix marks internal symbols that carry no stability guarantee and are never installed.

## Continuous integration

Four GitHub Actions workflows guard the tree. Each pins its action versions and runs with least privilege, and keeping the default branch green is the merge gate.

**Build and test.** Configures, builds under warnings as errors, and runs CTest across Linux, macOS, and Windows in both Debug and Release configurations. A separate job on Linux repeats the build with AddressSanitizer and UndefinedBehaviorSanitizer enabled so that memory and undefined behavior problems surface early.

**Format.** Runs `clang-format` in dry run mode with warnings as errors over every tracked C source and header, so formatting stays consistent across the whole codebase and style never becomes a review topic.

**Docs.** Checks that relative Markdown links resolve, so documentation does not rot into dead links.

**Release.** On a version tag, it builds and packages artifacts on every platform with CPack and attaches them to a draft GitHub release for a maintainer to review and publish.

No secrets are required to build or test. Only the release workflow uses the built in `GITHUB_TOKEN`.

## Contributing

Contributions are welcome. The `main` branch is always green and always releasable, so nobody commits to it directly. Every change lands through a reviewed pull request.

### Branch strategy

Branch from `main` with a typed prefix that names the kind of work: `feature/<slug>`, `fix/<slug>`, `docs/<slug>`, or `perf/<slug>`. Rebase on `main` before opening a pull request and keep history linear where practical.

### Pull request workflow

1. Branch from `main` and make your change.
2. Build and test locally. The sequence `cmake --preset dev`, then `cmake --build --preset dev`, then `ctest --preset dev` must pass.
3. Format and lint with `clang-format` and `clang-tidy`. Both configs live at the repository root.
4. Open the pull request and fill in the template.
5. CI must be green on every platform, and at least one maintainer review is required, before the change merges.

### Review checklist

Reviewers confirm that each pull request:

- Preserves correctness first. Output stays token exact where that applies, and every new kernel is tested against a scalar reference.
- Honors the dependency rules, adding no forbidden edge and no cycle, and keeping the core free of backends, frontends, and tools.
- Respects the API tier, so nothing internal leaks into a public header.
- Follows the error model, returning `hb_status`, never calling `exit()` in the library, and preserving the true operating system error.
- Documents ownership for every new pointer parameter, and states the thread safety contract for anything that touches shared state.
- Adds or updates tests that pass, with sanitizers clean for any code that touches memory.
- Leaves no stale comments and no magic numbers.

### Labels

Kind of change: `type:bug`, `type:feature`, `type:docs`, `type:perf`, `type:refactor`.
Subsystem: `area:runtime`, `area:memory`, `area:streaming`, `area:scheduler`, `area:tensor`, `area:backend`, `area:model`, `area:tokenizer`, `area:cli`, `area:build`, `area:ci`.
Triage: `good-first-issue`, `help-wanted`, `blocked`.

## Versioning and releases

The public ABI follows semantic versioning. Before version 1.0, on a `0.y.z` line, a minor bump may break the ABI, but only with a changelog entry that explains the break. The version has a single source of truth, the `project(... VERSION x.y.z)` call in the root `CMakeLists.txt`, and the `HB_VERSION` macros in the public header are derived from it rather than being edited by hand. To cut a release, bump the project version, then push a `v<x.y.z>` tag on `main`. The tag triggers the release workflow, which builds and packages on every platform and drafts a GitHub release with the artifacts attached.

## Design rationale

A few decisions shape the whole codebase, and knowing why they were made explains most of the structure you will encounter.

**Why C17.** C gives direct control over memory layout and a small, stable ABI that any language can call, which matters for a library meant to be embedded widely. C17 in particular is broadly supported by GCC, Clang, and MSVC, so the same source builds on all three tier one platforms without a heavier toolchain.

**Why zero runtime dependencies.** Every required dependency is something a user must install, a version that can conflict, and a surface that can break. By depending on nothing at runtime, the engine stays easy to build, easy to embed, and predictable across machines. Optional accelerators like CUDA are the only exception, and they are strictly opt in at build time.

**Why strict layering.** Allowing modules to depend only downward keeps the dependency graph acyclic. That is not bookkeeping for its own sake. It is what lets a single module be built and tested in isolation, what keeps compile times low, and what makes it possible to reason about one part of the system without holding the whole thing in your head.

**Why a data driven model adapter.** Baking a specific architecture into the engine would mean editing the core for every new model. Describing a model as a descriptor plus a forward graph of reusable node types turns support for a new architecture into new data rather than new engine code, and it is what lets dense and Mixture of Experts models share one runtime.

**Why the correctness contract is absolute.** The entire value of streaming from disk is that it produces the same answer as running fully in memory. If placement, caching, prefetch, or speculation could change the output, the engine would be trading correctness for speed, which defeats the purpose. Making that boundary a hard rule is what makes the optimizations safe to pursue aggressively.

## Security and reporting

Because the engine reads model files and, in the server frontend, will accept requests over a network, input handling is treated as a security surface rather than an afterthought. The error model requires malformed input to be reported as a status rather than trusted, the `HB_ERR_CORRUPT` code exists specifically for data that fails a container or bounds check, and the sanitizer builds in continuous integration exist to catch memory safety problems before they ship.

If you discover a security sensitive issue, please report it privately to the maintainer rather than opening a public issue, so that a fix can be prepared before the details are widely known. Non sensitive bugs and feature ideas are welcome as ordinary issues using the templates provided in the repository.

## Roadmap

The engine is delivered in milestones. The foundation is in place today, and inference capability lands progressively.

| Milestone | Focus |
|-----------|-------|
| M1 | Foundations: the module tree, build system, public ABI, and CI. |
| M2 | Tensor runtime and CPU compute kernels. |
| M3 | Model adapter and a working forward pass. |
| M4 | Weight streaming and the cache hierarchy. |
| M5 | The scheduler and I/O compute overlap. |
| M6 | Speculative execution. |
| M7 | GPU backends for CUDA and Metal. |
| M8 | Serving through the CLI and the HTTP server. |

## Frequently asked questions

**Can I run a model with this today?**
Not yet. The current tree is a foundation: it compiles, it is tested, and it exposes a stable public interface, but the forward pass and model loading arrive in later milestones. The version and status functions are the only implemented public calls right now.

**How can a model larger than my RAM run without being slower or less accurate than a smaller one?**
Accuracy does not change, because placement never alters the computation. The same quantized weights run the same kernels regardless of which tier they came from. Speed is preserved by the layout and caching strategy. Expert weights are stored so they read in large sequential I/O, the most recently and frequently used experts stay cached in RAM, and the scheduler prefetches the next weights while the current layer computes, so the disk is hidden behind work that is already happening.

**Why does this help Mixture of Experts models in particular?**
A Mixture of Experts model activates only a small subset of its experts per token, so at any instant most of the model is idle and does not need to occupy fast memory. Dense models activate all of their weights every token, so they benefit far less from streaming and are better served by simply fitting in memory. Hummingbird runs both, but streaming is where sparse models win.

**Do I need a GPU?**
No. The reference CPU backend is always built and is the correctness baseline. GPU backends for CUDA and Metal are optional accelerators that are compiled in only when their toolchains are available, and the engine always falls back to the CPU when a backend cannot serve an operation.

**Does it depend on any third party libraries at runtime?**
No. The engine is written in portable C17 with no required third party runtime dependencies. There is no mandatory BLAS and no Python in the runtime path.

**What model files does it target?**
The design targets safetensors as the on disk model format, with a model described to the engine as a descriptor plus a forward graph. Offline tooling for conversion lives under `tools/` and grows as the format support lands.

**Which platforms are supported?**
Linux, macOS, and Windows are the tier one platforms, and continuous integration builds and tests on all three in both Debug and Release configurations.

**How is this different from the project that inspired it?**
Colibrì proved the streaming idea on a specific model. Hummingbird is an independent, from scratch redesign into a modular, general purpose runtime with a layered architecture, a stable public ABI, a pluggable backend boundary, and a data driven model adapter, so that many architectures share one engine.

## Glossary

| Term | Meaning |
|------|---------|
| Mixture of Experts | A model design where each layer holds many expert subnetworks and a router selects only a few of them per token, so only a fraction of the weights is active at a time. |
| Dense model | A model that activates all of its weights for every token, the opposite of a sparse Mixture of Experts model. |
| Expert | One of the many subnetworks inside a Mixture of Experts layer, selected on demand by the router. |
| Router | The small network that decides which experts handle a given token. |
| Resident weight | A weight that is needed for every token and is kept in the fastest available memory. |
| Streamable weight | A weight, typically an expert, that lives on disk and is read into faster memory when needed. |
| Weight class | The classification attached to each tensor that decides how it is placed and whether its precision is pinned. |
| Coalesced read | A single large sequential disk read that replaces many small scattered ones. |
| Zero copy view | A view directly into a loaded buffer that avoids copying the data again before use. |
| Quantization | Representing weights with fewer bits than full floating point to save storage and memory. |
| KV cache | The stored keys and values from previous tokens that let attention avoid recomputing the whole sequence each step. |
| Backend | A compute implementation, such as CPU, CUDA, or Metal, that runs operations behind the stable backend ABI. |
| ABI | Application binary interface, the stable contract that lets compiled code link against the library across versions. |
| Forward graph | The skeleton of typed operation nodes that describes one pass of a model. |

## Acknowledgements

Hummingbird is inspired by [Colibrì](https://github.com/JustVugg/colibri), a tiny, zero dependency C engine that runs GLM 5.2, a 744 billion parameter Mixture of Experts model, on a consumer machine with roughly 25 GB of RAM by streaming experts from disk. Colibrì demonstrated that the idea works in practice. Hummingbird is an independent, from scratch redesign of that idea into a modular, general purpose runtime, and where it adapts approaches from Colibrì, which is also Apache 2.0 licensed, that lineage is attributed.

## License

Hummingbird is released under the [Apache License, Version 2.0](LICENSE), a permissive open source license. In plain terms you are free to use, modify, and distribute the software, for private or commercial purposes, provided that you preserve the copyright and license notices and state significant changes you make. The license also includes an explicit grant of patent rights from contributors to users, which gives downstream users protection that more minimal licenses do not, and it provides the software on an as is basis without warranty.

The `LICENSE` file in this repository is currently a placeholder that records the project's intent to ship under Apache 2.0 from its very first commit. Before the first public release it will be replaced with the full, verbatim Apache 2.0 text, and source files will carry `SPDX-License-Identifier: Apache-2.0` headers. The choice of Apache 2.0 matches the license of the Colibrì project that inspired this work, which keeps the lineage clean and the combined use unambiguous.

Built by [@prayangshuuu](https://github.com/prayangshuuu).
