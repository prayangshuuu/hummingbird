# tests/fuzz — libFuzzer targets for security-critical entry points.
#
# These targets are NOT built by default (they require a Clang toolchain with
# libFuzzer support). Enable with:
#
#   cmake -S . -B build-fuzz -G Ninja \
#         -DCMAKE_C_COMPILER=clang \
#         -DCMAKE_BUILD_TYPE=Debug \
#         -DHB_BUILD_FUZZ=ON \
#         -DCMAKE_C_FLAGS="-fsanitize=fuzzer,address,undefined"
#
# Each target writes its corpus to a `corpus/<target-name>/` directory.
# Seed corpus entries live in `tests/fuzz/seeds/`.
#
# Reference: https://llvm.org/docs/LibFuzzer.html

if(NOT HB_BUILD_FUZZ)
    return()
endif()

# Require Clang (libFuzzer is Clang-only).
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(WARNING "HB_BUILD_FUZZ=ON requires Clang; skipping fuzz targets")
    return()
endif()

# ── fuzz_model_loader ────────────────────────────────────────────────────────
# Fuzzes hbi_model_load() against arbitrary byte sequences masquerading as
# model files.  Tests: Safetensors parser, format detection, manifest
# validation, tensor entry iteration.
add_executable(fuzz_model_loader fuzz_model_loader.c)
target_link_libraries(fuzz_model_loader PRIVATE hummingbird hb_model)
target_include_directories(fuzz_model_loader PRIVATE
    "${HB_SRC_DIR}"
    "${HB_INCLUDE_DIR}")
hb_apply_common(fuzz_model_loader)
# libFuzzer replaces main(); do not strip it.
target_link_options(fuzz_model_loader PRIVATE -fsanitize=fuzzer)

# ── Seed corpus target ───────────────────────────────────────────────────────
# Copy seed files to the build tree so the fuzzer can use them out-of-the-box.
file(GLOB FUZZ_SEEDS "${CMAKE_CURRENT_SOURCE_DIR}/seeds/*")
foreach(seed ${FUZZ_SEEDS})
    get_filename_component(seed_name "${seed}" NAME)
    configure_file("${seed}" "${CMAKE_CURRENT_BINARY_DIR}/seeds/${seed_name}" COPYONLY)
endforeach()
