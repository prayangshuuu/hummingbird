# Getting Started

<cite>
**Referenced Files in This Document**
- [README.md](file://README.md)
- [CMakeLists.txt](file://CMakeLists.txt)
- [install.sh](file://install.sh)
- [include/hummingbird/hummingbird.h](file://include/hummingbird/hummingbird.h)
- [src/hummingbird.c](file://src/hummingbird.c)
- [examples/version.c](file://examples/version.c)
- [frontends/cli/main.c](file://frontends/cli/main.c)
- [frontends/server/main.c](file://frontends/server/main.c)
</cite>

## Table of Contents
1. [Introduction](#introduction)
2. [System Requirements](#system-requirements)
3. [Installation](#installation)
4. [Quick Start](#quick-start)
5. [Verification](#verification)
6. [Troubleshooting](#troubleshooting)
7. [Next Steps](#next-steps)

## Introduction
This guide helps you install Hummingbird and run your first inference on Linux, macOS, and Windows. You will build the project from source using CMake, verify the installation, and execute a minimal workflow that loads a model and performs inference through the provided frontends or examples.

## System Requirements
- A modern C/C++ compiler:
  - GCC or Clang on Linux and macOS
  - MSVC (Visual Studio Build Tools) on Windows
- CMake 3.20 or newer
- A compatible operating system:
  - Linux (glibc-based distributions)
  - macOS (recent versions)
  - Windows 10/11 with Visual Studio Build Tools
- Optional acceleration backends:
  - CUDA backend requires an NVIDIA GPU and a matching CUDA toolkit
  - Metal backend requires Apple Silicon/macOS with Metal support
- Disk space for sources, build artifacts, and example models

Notes:
- The repository uses CMake as the primary build system.
- Backends are conditionally compiled; CPU-only builds work without extra dependencies.

**Section sources**
- [CMakeLists.txt:1-80](file://CMakeLists.txt#L1-L80)

## Installation
Choose one of the following methods based on your platform and needs.

### Option A: Automated installer script
The repository includes a convenience script to streamline setup and build steps.

- Linux/macOS
  - Open a terminal and run:
    - bash install.sh
  - The script typically configures, builds, and installs targets defined by CMake.
- Windows
  - Use Git Bash or WSL to run the same command, or follow Option B below.

After running the script, verify the installation as described in the Verification section.

**Section sources**
- [install.sh](file://install.sh)

### Option B: Manual CMake build (recommended for full control)
Use this method if you prefer explicit control over configuration flags and installation paths.

- Linux/macOS
  - Create a build directory:
    - mkdir -p build && cd build
  - Configure:
    - cmake ..
  - Build:
    - cmake --build . --config Release
  - Install (optional):
    - sudo cmake --install .
- Windows (MSVC)
  - Open Developer Command Prompt for VS or use CMake GUI:
    - mkdir build && cd build
    - cmake .. -G "Visual Studio 17 2022"
    - cmake --build . --config Release
    - cmake --install . --config Release

Optional: Enable specific backends during configuration (example patterns):
- CPU-only: default behavior when no GPU options are enabled
- CUDA: enable CUDA-related CMake options if available
- Metal: enable Metal-related CMake options on macOS

Verify the installation as described in the Verification section.

**Section sources**
- [CMakeLists.txt:1-80](file://CMakeLists.txt#L1-L80)

## Quick Start
This quick start walks you through the minimum viable workflow: build, locate binaries/examples, load a model, and perform inference.

### Step 1: Build the project
Follow the installation instructions above for your platform. Ensure the build completes successfully.

### Step 2: Locate built targets
After building, you should have access to:
- CLI frontend executable
- Server frontend executable
- Example programs under the examples directory

These are produced by the CMake configuration and can be found in your build output directory.

**Section sources**
- [frontends/cli/main.c](file://frontends/cli/main.c)
- [frontends/server/main.c](file://frontends/server/main.c)
- [examples/version.c](file://examples/version.c)

### Step 3: Run a simple example
Run the version utility to confirm the runtime is working:
- Execute the built version example from your build directory.
- Confirm it prints version information without errors.

This demonstrates that the core library and public headers are correctly linked.

**Section sources**
- [examples/version.c](file://examples/version.c)
- [include/hummingbird/hummingbird.h](file://include/hummingbird/hummingbird.h)

### Step 4: Load a model and perform inference
Use the CLI frontend to run inference against a supported model file:
- Invoke the CLI executable with the path to a model file and any required arguments.
- If the model loads and runs, you have completed the minimum viable workflow.

If you prefer a server mode, start the server frontend and send requests according to its usage instructions.

**Section sources**
- [frontends/cli/main.c](file://frontends/cli/main.c)
- [frontends/server/main.c](file://frontends/server/main.c)

## Verification
Perform these checks to ensure a proper setup:
- Version check:
  - Run the version example and confirm it outputs version details.
- Library presence:
  - Verify that the expected shared/static libraries and headers are installed or present in your build tree.
- Frontend executables:
  - Confirm that the CLI and server executables exist and run without immediate errors.
- Basic inference:
  - Run the CLI with a small model to complete an inference cycle end-to-end.

If all checks pass, your environment is ready for development and experimentation.

**Section sources**
- [examples/version.c](file://examples/version.c)
- [include/hummingbird/hummingbird.h](file://include/hummingbird/hummingbird.h)
- [frontends/cli/main.c](file://frontends/cli/main.c)
- [frontends/server/main.c](file://frontends/server/main.c)

## Troubleshooting
Common issues and resolutions:

- CMake not found or too old
  - Install or update CMake to at least version 3.20.
  - On Windows, ensure the CMake generator matches your Visual Studio version.

- Compiler toolchain issues
  - Linux: install build-essential or equivalent packages.
  - macOS: install Xcode Command Line Tools.
  - Windows: install Visual Studio Build Tools with C++ workload.

- Missing optional dependencies for backends
  - CUDA: ensure the CUDA toolkit is installed and discoverable by CMake.
  - Metal: ensure you are on a supported macOS version with Metal enabled.

- Permission errors during install
  - Use elevated privileges for system-wide installs or configure a user-local prefix.

- Linker errors or missing symbols
  - Rebuild after ensuring all subprojects and third-party modules are configured.
  - Clean the build directory and reconfigure if necessary.

- Frontend fails to find libraries at runtime
  - Adjust your dynamic linker settings (e.g., LD_LIBRARY_PATH on Linux, DYLD_LIBRARY_PATH on macOS) or install to standard locations.

If problems persist, consult the top-level README and CMake configuration for additional options and notes.

**Section sources**
- [README.md](file://README.md)
- [CMakeLists.txt:1-80](file://CMakeLists.txt#L1-L80)

## Next Steps
- Explore the public API headers to understand the core interfaces.
- Experiment with different model formats and quantization options if supported.
- Try the server frontend for request-driven inference scenarios.
- Review the examples directory for additional usage patterns.

[No sources needed since this section provides general guidance]