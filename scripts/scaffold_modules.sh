#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# scaffold_modules.sh — generate the src/ module skeletons for Hummingbird.
#
# This is a one-shot bootstrap generator (Phase 4). It creates, for each core
# module, a README, a core-public header, a private header, a minimal compiling
# implementation, a unit-test placeholder, and a CMakeLists.txt. It writes NO
# inference logic — every module exposes only a trivial `name()`/`selftest()`
# pair so the tree compiles, links, and tests green from day one.
#
# Safe to re-run: files are overwritten from the templates below.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"

# module | one-line purpose | space-separated direct dependencies (module names)
#
# The dependency lists below are the *enforced* build graph. It is acyclic:
# `common` sits at the bottom, `runtime` at the top. CMake will error on a cycle,
# so this table is validated every configure.
MODULES=$(cat <<'EOF'
common|Foundational types, status codes, and small header-only utilities shared by every module.|
platform|Portability shim: OS/compiler/arch differences (file I/O, aligned alloc, threads, time) behind one API.|common
logging|Structured, leveled logging — distinct from the machine-readable telemetry stream.|common platform
profiler|Timing and counter instrumentation; names the bottleneck. Emits telemetry, never log spam.|common platform logging
config|Typed, validated configuration (file + flags + env override) with an introspectable schema.|common logging
device|Device discovery and capability description (CPU features, GPUs) independent of any backend.|common platform
tensor|Quantized tensor representation and CPU matmul dispatch (exact + integer-dot regimes).|common platform
quant|Quantization format registry: pack/unpack contracts and format detection.|common tensor
memory|Tier model (VRAM/RAM/NVMe), RAM budget, OOM/RSS guards, tagged allocators.|common platform logging
stream|Weight streaming: contiguity to single coalesced read to zero-copy views (hard invariant).|common platform memory logging
kv|KV-cache abstraction: MHA/GQA and compressed-latent (MLA) layouts; save/restore.|common memory tensor
backend|Stable extern-C backend ABI (CPU reference + optional CUDA/Metal) with per-tensor CPU fallback.|common device tensor
tokenizer|Tokenizer adapter interface (BPE/unigram/...); encode/decode.|common
graph|Forward-graph node types (RMSNORM/ATTENTION/MOE/MLP/RESIDUAL) and typed op-module registry.|common tensor
model|Model adapter: descriptor + forward graph + tokenizer ref + quant classes. New models are additive.|common quant graph tokenizer
executor|Walks a forward graph and dispatches each op node to its typed module on the active backend.|common graph tensor backend kv memory
scheduler|Overlaps I/O with compute and prefetches (PIPE/PILOT). Speculative actions never change output.|common memory stream backend
context|Per-session decode context: KV state, sampling state, run mode.|common memory kv model
runtime|Orchestrator: owns the forward loop, sequences layers, drives the scheduler, produces logits.|common config logging profiler model executor scheduler stream context memory backend
EOF
)

upper() { printf '%s' "$1" | tr '[:lower:]' '[:upper:]'; }

gen_module() {
  local name="$1" desc="$2" deps="$3"
  local dir="$SRC/$name"
  local guard; guard="HB_$(upper "$name")_H"
  local iguard; iguard="HB_$(upper "$name")_INTERNAL_H"
  mkdir -p "$dir"

  # ── README.md ──────────────────────────────────────────────────────────────
  {
    printf '# `%s` module\n\n' "$name"
    printf '%s\n\n' "$desc"
    printf '## Status\n\nScaffold only — no inference logic yet. Exposes `hbi_%s_name()` and `hbi_%s_selftest()` so the tree compiles and tests green.\n\n' "$name" "$name"
    printf '## Layout\n\n'
    printf '| File | Role |\n|------|------|\n'
    printf '| `%s.h` | Core-public header (`hbi_%s_*`), included by other modules. |\n' "$name" "$name"
    printf '| `%s_internal.h` | Private header — implementation details, not for other modules. |\n' "$name"
    printf '| `%s.c` | Implementation. |\n' "$name"
    printf '| `%s_test.c` | Unit-test placeholder (CTest target `%s`). |\n' "$name" "$name"
    printf '| `CMakeLists.txt` | Build target `hb_%s`. |\n\n' "$name"
    printf '## Allowed dependencies\n\n'
    if [ -z "$deps" ]; then
      printf 'None. This module is foundational and must not depend on any other Hummingbird module.\n\n'
    else
      printf 'This module may depend **only** on: '
      local first=1 d
      for d in $deps; do
        if [ $first -eq 1 ]; then printf '`%s`' "$d"; first=0; else printf ', `%s`' "$d"; fi
      done
      printf '.\n\n'
    fi
    printf '## Forbidden dependencies\n\n'
    printf 'Any module not listed above, and any cyclic dependency. Frontends, tools, and backends-as-plugins must never be pulled in here. See `docs/architecture/03-dependency-graph.md` for the full rule set.\n'
  } > "$dir/README.md"

  # ── public header ────────────────────────────────────────────────────────────
  {
    printf '/* %s.h — %s\n' "$name" "$desc"
    printf ' *\n * Core-public header for the `%s` module. Other modules include this;\n' "$name"
    printf ' * external embedders use <hummingbird.h> instead. Symbols are prefixed\n'
    printf ' * `hbi_` (internal, no stability guarantee). See docs/architecture.\n */\n'
    printf '#ifndef %s\n#define %s\n\n' "$guard" "$guard"
    printf '#include "common/common.h"\n\n'
    printf '#ifdef __cplusplus\nextern "C" {\n#endif\n\n'
    printf '/* Human-readable module name. Never NULL. */\n'
    printf 'const char *hbi_%s_name(void);\n\n' "$name"
    printf '/* Compile-time self-check. Returns HB_OK when the module is well-formed. */\n'
    printf 'hb_status hbi_%s_selftest(void);\n\n' "$name"
    printf '#ifdef __cplusplus\n}\n#endif\n\n'
    printf '#endif /* %s */\n' "$guard"
  } > "$dir/$name.h"

  # ── private header ───────────────────────────────────────────────────────────
  {
    printf '/* %s_internal.h — private to the `%s` module.\n' "$name" "$name"
    printf ' *\n * Nothing here is visible to other modules. Implementation details,\n'
    printf ' * internal structs, and static-helper prototypes live here as the module grows.\n */\n'
    printf '#ifndef %s\n#define %s\n\n' "$iguard" "$iguard"
    printf '#include "%s/%s.h"\n\n' "$name" "$name"
    printf '/* (scaffold) internal declarations go here. */\n\n'
    printf '#endif /* %s */\n' "$iguard"
  } > "$dir/${name}_internal.h"

  # ── implementation ───────────────────────────────────────────────────────────
  {
    printf '/* %s.c — %s */\n' "$name" "$desc"
    printf '#include "%s/%s_internal.h"\n\n' "$name" "$name"
    printf 'const char *hbi_%s_name(void) {\n    return "%s";\n}\n\n' "$name" "$name"
    printf 'hb_status hbi_%s_selftest(void) {\n    /* Scaffold: no invariants to check yet. */\n    return HB_OK;\n}\n' "$name"
  } > "$dir/$name.c"

  # ── unit-test placeholder ────────────────────────────────────────────────────
  {
    printf '/* %s_test.c — unit-test placeholder for the `%s` module.\n' "$name" "$name"
    printf ' * Replace with real cases as the module gains behavior. */\n'
    printf '#include "%s/%s.h"\n\n' "$name" "$name"
    printf '#include <stdio.h>\n#include <string.h>\n\n'
    printf 'int main(void) {\n'
    printf '    if (hbi_%s_selftest() != HB_OK) {\n' "$name"
    printf '        fprintf(stderr, "%%s: selftest failed\\n", hbi_%s_name());\n' "$name"
    printf '        return 1;\n    }\n'
    printf '    if (strcmp(hbi_%s_name(), "%s") != 0) {\n' "$name" "$name"
    printf '        fprintf(stderr, "unexpected module name: %%s\\n", hbi_%s_name());\n' "$name"
    printf '        return 1;\n    }\n'
    printf '    printf("[ok] %%s\\n", hbi_%s_name());\n' "$name"
    printf '    return 0;\n}\n'
  } > "$dir/${name}_test.c"

  # ── CMakeLists.txt ───────────────────────────────────────────────────────────
  {
    printf '# hb_%s — %s\n' "$name" "$desc"
    printf 'add_library(hb_%s STATIC %s.c)\n' "$name" "$name"
    printf 'add_library(hummingbird::%s ALIAS hb_%s)\n\n' "$name" "$name"
    printf 'target_include_directories(hb_%s\n' "$name"
    printf '    PUBLIC "${HB_SRC_DIR}" "${HB_INCLUDE_DIR}")\n\n'
    if [ -n "$deps" ]; then
      printf 'target_link_libraries(hb_%s PUBLIC' "$name"
      local d
      for d in $deps; do printf ' hb_%s' "$d"; done
      printf ')\n\n'
    fi
    printf 'hb_apply_common(hb_%s)\n\n' "$name"
    printf 'if(HB_BUILD_TESTS)\n'
    printf '    add_executable(test_%s %s_test.c)\n' "$name" "$name"
    printf '    target_link_libraries(test_%s PRIVATE hb_%s)\n' "$name" "$name"
    printf '    hb_apply_common(test_%s)\n' "$name"
    printf '    add_test(NAME %s COMMAND test_%s)\n' "$name" "$name"
    printf 'endif()\n'
  } > "$dir/CMakeLists.txt"

  printf 'generated module: %s\n' "$name"
}

# ── src/CMakeLists.txt — add modules in dependency order + aggregate library ──
gen_src_cmake() {
  {
    printf '# Auto-generated by scripts/scaffold_modules.sh (Phase 4 bootstrap).\n'
    printf '# Modules are listed bottom-up (dependencies before dependents).\n\n'
    local name rest
    while IFS='|' read -r name desc deps; do
      [ -z "$name" ] && continue
      printf 'add_subdirectory(%s)\n' "$name"
    done <<< "$MODULES"
    printf '\n# ── Aggregate library: the embeddable engine (stable C ABI) ──\n'
    printf 'add_library(hummingbird STATIC hummingbird.c)\n'
    printf 'add_library(hummingbird::hummingbird ALIAS hummingbird)\n'
    printf 'target_include_directories(hummingbird PUBLIC "${HB_INCLUDE_DIR}" PRIVATE "${HB_SRC_DIR}")\n'
    printf '# Linking runtime transitively pulls in every core module.\n'
    printf 'target_link_libraries(hummingbird PUBLIC hb_runtime)\n'
    printf 'hb_apply_common(hummingbird)\n\n'
    printf 'if(HB_BUILD_TESTS)\n'
    printf '    add_executable(test_hummingbird hummingbird_test.c)\n'
    printf '    target_link_libraries(test_hummingbird PRIVATE hummingbird)\n'
    printf '    hb_apply_common(test_hummingbird)\n'
    printf '    add_test(NAME hummingbird_api COMMAND test_hummingbird)\n'
    printf 'endif()\n'
  } > "$SRC/CMakeLists.txt"
  printf 'generated: src/CMakeLists.txt\n'
}

while IFS='|' read -r name desc deps; do
  [ -z "$name" ] && continue
  gen_module "$name" "$desc" "$deps"
done <<< "$MODULES"

gen_src_cmake
printf 'done.\n'
