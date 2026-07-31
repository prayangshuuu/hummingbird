# Hummingbird Documentation

> [!NOTE]
> For AI assistant context, see `.claude/CLAUDE.md` instead.

## Getting Started

- [Getting Started](getting-started/Getting%20Started.md)
- [Project Overview](getting-started/Project%20Overview.md)
- [Technology Stack](getting-started/Technology%20Stack.md)
- [Roadmap](getting-started/ROADMAP.md)
- [Business Glossary](getting-started/Business%20Glossary.md)

## Architecture

**Overview**
- [Architecture Overview](architecture/overview/Architecture%20Overview.md)
- [Core Concepts](architecture/overview/Core%20Concepts.md)
- [Architecture Design (Legacy)](architecture/overview/architecture_design.md)

**Execution & Scheduling**
- [Execution Engine](architecture/execution/Execution%20Engine.md)
- [Execution Pipeline](architecture/execution/Execution%20Pipeline.md)
- [Graph Optimization and Execution Planning](architecture/execution/Graph%20Optimization%20and%20Execution%20Planning.md)
- [Planning and Optimization](architecture/execution/Planning%20and%20Optimization.md)
- [Task Scheduling](architecture/execution/Task%20Scheduling.md)
- [Runtime Management and Lifecycle Control](architecture/execution/Runtime%20Management%20and%20Lifecycle%20Control.md)
- [Runtime Sequence Diagrams](architecture/execution/runtime_sequence_diagrams.md)

**Memory & Devices**
- [Memory Pool Architecture](architecture/memory_and_devices/Memory%20Pool%20Architecture.md)
- [Device Abstraction Layer](architecture/memory_and_devices/Device%20Abstraction%20Layer.md)

**Threading & Streaming**
- [Thread Pool Management](architecture/threading/Thread%20Pool%20Management.md)
- [Streaming Processing](architecture/streaming/Streaming%20Processing.md)

**Security**
- [Authentication & Security](architecture/security/Authentication%20&%20Security.md)

**Design Decisions**
- [CMake Build System](architecture/design_decisions/CMake%20Build%20System.md)
- [CMake-Based Modular Build System](architecture/design_decisions/CMake-Based%20Modular%20Build%20System%20with%20Presets%20and%20Sanitizers.md)
- [Pure C Runtime](architecture/design_decisions/No%20frontend%20style%20system%20—%20pure%20C%20runtime.md)
- [Error Records](architecture/design_decisions/Thread-Local%20Error%20Records%20with%20Stable%20Status%20Codes.md)
- [Configuration System](architecture/design_decisions/Typed,%20Schema-Driven%20Configuration%20System%20with%20Precedence-Aware%20Loading.md)
- [Vendoring Policy](architecture/design_decisions/Zero-Dependency%20C%20Runtime%20with%20Vendoring%20Policy.md)
- [Implementation Summary](architecture/design_decisions/implementation_summary.md)

**Detailed Specifications**
- [Detailed Architecture Specs](architecture/specs/) ← numbered 01–13

## API Reference

**Core API**
- [Core API](api/core/Core%20API.md)
- [Runtime API Reference](api/core/runtime_api_reference.md)

**HTTP & REST**
- [HTTP Server API](api/rest/HTTP%20Server%20API.md)
- [REST API Endpoints](api/rest/REST%20API%20Endpoints.md)

**WebSocket**
- [WebSocket Streaming API](api/websocket/WebSocket%20Streaming%20API.md)

**Configuration**
- [Configuration API](api/configuration/Configuration%20API.md)
- [Experimental API](api/configuration/Experimental%20API.md)

**Reference & CLI**
- [API Reference](api/reference/API%20Reference.md)
- [CLI Interface](api/reference/CLI%20Interface.md)

## Backends

**Overview**
- [Backend Architecture Overview](backends/overview/Backend%20Architecture%20Overview.md)
- [Backend System](backends/overview/Backend%20System.md)

**Implementations**
- [CPU Backend Implementation](backends/cpu/CPU%20Backend%20Implementation.md)
- [CUDA Backend (GPU Acceleration)](backends/cuda/CUDA%20Backend%20%28GPU%20Acceleration%29.md)
- [CUDA Backend Accelerator](backends/cuda/CUDA%20Backend%20Accelerator.md)
- [Metal Backend (Apple GPU)](backends/metal/Metal%20Backend%20%28Apple%20GPU%29.md)
- [Metal GPU Backend](backends/metal/Metal%20GPU%20Backend.md)

**Development**
- [Custom Backend Development Guide](backends/development/Custom%20Backend%20Development%20Guide.md)
- [Hardware-Specific Tuning](backends/development/Hardware-Specific%20Tuning.md)

## Guides

**Development**
- [Development Guide](guides/development/Development%20Guide.md)
- [Custom Plugin Development](guides/development/Custom%20Plugin%20Development.md)
- [Advanced Topics](guides/development/Advanced%20Topics.md)
- [Coding Conventions](guides/development/coding_conventions.md)

**Performance**
- [Performance Optimization](guides/performance/Performance%20Optimization.md)
- [Performance Tuning and Profiling](guides/performance/Performance%20Tuning%20and%20Profiling.md)
- [Memory Optimization Strategies](guides/performance/Memory%20Optimization%20Strategies.md)
- [Model Quantization](guides/performance/Model%20Quantization.md)

**Operations**
- [Deployment & Configuration](guides/operations/Deployment%20&%20Configuration.md)
- [Debugging and Profiling Tools](guides/operations/Debugging%20and%20Profiling%20Tools.md)

## Module Deep Dives

**Models**
- [Model Formats and Loading](modules/models/Model%20Formats%20and%20Loading.md)
- [Model Management](modules/models/Model%20Management.md)
- [Model Metadata and Configuration](modules/models/Model%20Metadata%20and%20Configuration.md)

**Tensors & KV**
- [Tensor Operations and Types](modules/tensors_and_kv/Tensor%20Operations%20and%20Types.md)
- [Key-Value Storage System](modules/tensors_and_kv/Key-Value%20Storage%20System.md)

**Interfaces**
- [Adapter Pattern Implementation](modules/interfaces/Adapter%20Pattern%20Implementation.md)
- [Frontend Interfaces](modules/interfaces/Frontend%20Interfaces.md)

**Core Modules**
- [Context Management](modules/core/Context%20Management.md)
- [Tokenizer Framework](modules/core/Tokenizer%20Framework.md)
- [Structured, Leveled Logging with Pluggable Sinks](modules/core/Structured,%20Leveled%20Logging%20with%20Pluggable%20Sinks.md)

## Testing & Quality

- [Testing Framework and Infrastructure](quality/Testing%20Framework%20and%20Infrastructure.md)
- [Benchmarking Suite and Methodology](quality/Benchmarking%20Suite%20and%20Methodology.md)
- [Code Quality and Standards](quality/Code%20Quality%20and%20Standards.md)
- [Profiling Tools and Techniques](quality/Profiling%20Tools%20and%20Techniques.md)

## RFCs

- [RFC-013: Runtime Orchestrator (original)](rfcs/RFC-013-runtime-orchestrator.md)
- [RFC-017: Tokenizer](rfcs/RFC-017-tokenizer.md)
- [RFC-019: Runtime Orchestrator (final)](rfcs/RFC-019-runtime-orchestrator.md)
- [RFC-020: Streaming Engine (DRAFT)](rfcs/RFC-020-DRAFT.md)

## Reports

- [First Inference Report](reports/implementation/first_inference_report.md)
- [Runtime Architecture Review](reports/reviews/runtime_architecture_review.md)
- [Runtime Certification](reports/certification/runtime_certification.md)
- [Runtime Implementation Report](reports/implementation/runtime_implementation_report.md)
- [Runtime Testing Report](reports/testing/runtime_testing_report.md)
- [Certification Audit](reports/audits/hummingbird_certification_audit.md)
- [Traceability Matrix](reports/certification/runtime_traceability_matrix.md)
