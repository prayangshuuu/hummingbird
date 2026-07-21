# ─────────────────────────────────────────────────────────────────────────────
# Hummingbird — root build.
#
# A general-purpose inference runtime for large open-source LLMs (dense + MoE).
# See docs/architecture/05-build-system.md for the full design. This file wires
# together: options → toolchain policy → the module tree (src/) → tests.
#
# PHASE 4: only scaffolding builds. No inference, kernels, or model loading yet.
# ─────────────────────────────────────────────────────────────────────────────
cmake_minimum_required(VERSION 3.20)

project(Hummingbird
    VERSION 0.0.0
    DESCRIPTION "General-purpose inference runtime for large open-source LLMs"
    HOMEPAGE_URL "https://github.com/your-org/hummingbird"
    LANGUAGES C)

# ── Guardrails ───────────────────────────────────────────────────────────────
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. Configure into a build directory, e.g.:\n"
        "  cmake -S . -B build")
endif()

# Let the project define its own standard even when built as a subdirectory.
set(HB_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(HB_IS_TOP_LEVEL ON)
endif()

# ── Language standard (ADR DD-001: C17) ──────────────────────────────────────
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Export compile_commands.json for clang-tidy / editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type for single-config generators.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# Where our helper modules live.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Shared paths used by every module's CMakeLists.
set(HB_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(HB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# ── Options: what to build ───────────────────────────────────────────────────
option(HB_BUILD_TESTS      "Build unit tests and register them with CTest" ON)
option(HB_BUILD_EXAMPLES   "Build example programs"                        ON)
option(HB_BUILD_BENCHMARKS "Build benchmarks"                              OFF)
option(HB_BUILD_TOOLS      "Build offline tools (converter, oracle, ...)"  ON)
option(HB_BUILD_FRONTENDS  "Build the hb CLI and server frontends"         ON)

# ── Options: correctness/hardening ───────────────────────────────────────────
option(HB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(HB_ENABLE_ASAN        "Enable AddressSanitizer"           OFF)
option(HB_ENABLE_UBSAN       "Enable UndefinedBehaviorSanitizer" OFF)

# ── Options: backends (compute plug-ins behind the ABI) ──────────────────────
# CPU is always built. The rest are opt-in and default OFF so a clean CPU-only
# build needs no special toolchain. Future backends are declared now so the
# option surface is stable, even though they are not yet implemented.
option(HB_BACKEND_CPU    "Build the CPU reference backend (always recommended)" ON)
option(HB_BACKEND_CUDA   "Build the CUDA backend (requires CUDA toolkit)"       OFF)
option(HB_BACKEND_METAL  "Build the Metal backend (Apple platforms only)"       OFF)
option(HB_BACKEND_ROCM   "Build the ROCm/HIP backend (future; not implemented)" OFF)
option(HB_BACKEND_VULKAN "Build the Vulkan backend (future; not implemented)"   OFF)

# ── Central compile/link policy applied to every target ──────────────────────
include(HBCompilerOptions)   # defines hb_apply_common()
include(HBSanitizers)        # wires ASan/UBSan into hb_apply_common's flags

# ── Testing ──────────────────────────────────────────────────────────────────
if(HB_BUILD_TESTS)
    enable_testing()
    include(CTest)

    # Header-only unit-test harness shared by every module's <mod>_test.c.
    # Test-only: never linked into the library or installed (see tests/support).
    add_library(hb_test_support INTERFACE)
    target_include_directories(hb_test_support INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/support")
endif()

# ── The engine (module tree + aggregate libhummingbird) ──────────────────────
add_subdirectory(src)

# ── Optional trees ───────────────────────────────────────────────────────────
if(HB_BACKEND_CPU OR HB_BACKEND_CUDA OR HB_BACKEND_METAL)
    add_subdirectory(backends)
endif()

if(HB_BUILD_FRONTENDS)
    add_subdirectory(frontends)
endif()

if(HB_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(HB_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(HB_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(HB_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ── Install + packaging ──────────────────────────────────────────────────────
# Minimal, correct install rules so `cmake --install` and CPack (release.yml)
# work today. The installed surface is the public header + the aggregate library.
# Backend/adapter install rules are added as those subsystems mature.
include(GNUInstallDirs)
install(DIRECTORY "${HB_INCLUDE_DIR}/hummingbird"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(TARGETS hummingbird
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(HB_BUILD_FRONTENDS)
    install(TARGETS hb hb-server
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(HB_IS_TOP_LEVEL)
    set(CPACK_PACKAGE_NAME "hummingbird")
    set(CPACK_PACKAGE_VENDOR "Hummingbird contributors")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_SOURCE_GENERATOR "TGZ")
    include(CPack)
endif()

# ── Configure summary ────────────────────────────────────────────────────────
if(HB_IS_TOP_LEVEL)
    message(STATUS "")
    message(STATUS "Hummingbird ${PROJECT_VERSION} — configuration summary")
    message(STATUS "  Build type ......... ${CMAKE_BUILD_TYPE}")
    message(STATUS "  C compiler ......... ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
    message(STATUS "  Warnings as errors . ${HB_WARNINGS_AS_ERRORS}")
    message(STATUS "  ASan / UBSan ....... ${HB_ENABLE_ASAN} / ${HB_ENABLE_UBSAN}")
    message(STATUS "  Tests .............. ${HB_BUILD_TESTS}")
    message(STATUS "  Backends: cpu=${HB_BACKEND_CPU} cuda=${HB_BACKEND_CUDA} metal=${HB_BACKEND_METAL} rocm=${HB_BACKEND_ROCM} vulkan=${HB_BACKEND_VULKAN}")
    message(STATUS "")
endif()
