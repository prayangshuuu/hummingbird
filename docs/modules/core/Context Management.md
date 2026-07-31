# Context Management

<cite>
**Referenced Files in This Document**
- [context.h](file://src/context/context.h)
- [context.c](file://src/context/context.c)
- [context_internal.h](file://src/context/context_internal.h)
- [context_test.c](file://src/context/context_test.c)
- [core.h](file://src/core/core.h)
- [core.c](file://src/core/core.c)
- [device.h](file://src/device/device.h)
- [device.c](file://src/device/device.c)
- [memory.h](file://src/memory/memory.h)
- [memory.c](file://src/memory/memory.c)
- [backend.h](file://src/backend/backend.h)
- [backend.c](file://src/backend/backend.c)
- [stream.h](file://src/stream/stream.h)
- [stream.c](file://src/stream/stream.c)
- [executor.h](file://src/executor/executor.h)
- [executor.c](file://src/executor/executor.c)
- [graph.h](file://src/graph/graph.h)
- [graph.c](file://src/graph/graph.c)
- [model.h](file://src/model/model.h)
- [model.c](file://src/model/model.c)
</cite>

## Table of Contents
1. [Introduction](#introduction)
2. [Project Structure](#project-structure)
3. [Core Components](#core-components)
4. [Architecture Overview](#architecture-overview)
5. [Detailed Component Analysis](#detailed-component-analysis)
6. [Dependency Analysis](#dependency-analysis)
7. [Performance Considerations](#performance-considerations)
8. [Troubleshooting Guide](#troubleshooting-guide)
9. [Conclusion](#conclusion)
10. [Appendices](#appendices)

## Introduction
This document explains Hummingbird’s Context Management system: how contexts are created, configured, and destroyed; how they encapsulate session state, resource isolation, and execution environment configuration; and how they relate to devices, memory pools, backends, streams, executors, graphs, and models. It also provides practical guidance for initialization patterns, multi-context usage, cleanup procedures, concurrent usage, error handling, scope management, and performance considerations.

## Project Structure
The Context subsystem is implemented under src/context with public headers and tests. It integrates with core runtime components such as device, memory, backend, stream, executor, graph, and model modules.

```mermaid
graph TB
subgraph "Context Subsystem"
CH["context.h"]
CI["context_internal.h"]
CC["context.c"]
CT["context_test.c"]
end
subgraph "Runtime Core"
COREH["core.h"]
COREC["core.c"]
end
subgraph "Execution & Graphs"
EXEH["executor.h"]
EXEC["executor.c"]
GRPH["graph.h"]
GRPC["graph.c"]
end
subgraph "Devices & Backends"
DEVH["device.h"]
DEVC["device.c"]
BEHH["backend.h"]
BEHC["backend.c"]
end
subgraph "Memory & Streams"
MEMH["memory.h"]
MEMC["memory.c"]
STRH["stream.h"]
STRC["stream.c"]
end
subgraph "Models"
MDLH["model.h"]
MDLC["model.c"]
end
CC --> CH
CC --> CI
CC --> COREC
CC --> DEVC
CC --> BEHC
CC --> MEMC
CC --> STRC
CC --> EXEC
CC --> GRPC
CC --> MDLC
```

**Diagram sources**
- [context.h](file://src/context/context.h)
- [context.c](file://src/context/context.c)
- [context_internal.h](file://src/context/context_internal.h)
- [core.h](file://src/core/core.h)
- [core.c](file://src/core/core.c)
- [device.h](file://src/device/device.h)
- [device.c](file://src/device/device.c)
- [backend.h](file://src/backend/backend.h)
- [backend.c](file://src/backend/backend.c)
- [memory.h](file://src/memory/memory.h)
- [memory.c](file://src/memory/memory.c)
- [stream.h](file://src/stream/stream.h)
- [stream.c](file://src/stream/stream.c)
- [executor.h](file://src/executor/executor.h)
- [executor.c](file://src/executor/executor.c)
- [graph.h](file://src/graph/graph.h)
- [graph.c](file://src/graph/graph.c)
- [model.h](file://src/model/model.h)
- [model.c](file://src/model/model.c)

**Section sources**
- [context.h](file://src/context/context.h)
- [context.c](file://src/context/context.c)
- [context_internal.h](file://src/context/context_internal.h)
- [context_test.c](file://src/context/context_test.c)

## Core Components
- Context object: Encapsulates the execution environment, including active device, memory allocator, backend selection, stream(s), executor, and graph registry. It isolates resources per context so that tensors, graphs, and models can be bound to a specific execution domain.
- Configuration: Context creation accepts configuration options (e.g., target device, backend, memory policy). These determine which backend implementation is used, where allocations occur, and how kernels are dispatched.
- Lifecycle: Creation initializes internal state and binds to selected resources; destruction releases all owned resources and resets state.

Key responsibilities:
- Resource isolation: Each context owns or references its own device, memory pool, backend instance, and execution primitives.
- Session state: Holds per-session settings like default stream, profiling flags, and logging verbosity.
- Execution environment: Provides handles to executor and graph registries scoped to the context.

**Section sources**
- [context.h](file://src/context/context.h)
- [context.c](file://src/context/context.c)
- [context_internal.h](file://src/context/context_internal.h)

## Architecture Overview
The Context acts as a central hub connecting devices, memory, backends, streams, executors, graphs, and models. The following diagram shows the primary relationships and data/control flow during typical operations.

```mermaid
classDiagram
class Context {
+create(config)
+destroy()
+get_device()
+get_memory_pool()
+get_backend()
+get_stream()
+get_executor()
+get_graph_registry()
}
class Device {
+init()
+select(id)
+info()
}
class Backend {
+init()
+dispatch(kernel, args)
+teardown()
}
class MemoryPool {
+alloc(size)
+free(ptr)
+stats()
}
class Stream {
+submit(task)
+sync()
}
class Executor {
+run(graph)
+schedule(tasks)
}
class GraphRegistry {
+register(name, graph)
+find(name)
}
class Model {
+load(path)
+infer(input)
}
Context --> Device : "owns/selects"
Context --> Backend : "initializes"
Context --> MemoryPool : "configures"
Context --> Stream : "creates/default"
Context --> Executor : "provides"
Context --> GraphRegistry : "hosts"
Model --> Context : "uses device/memory/backend"
```

**Diagram sources**
- [context.h](file://src/context/context.h)
- [context.c](file://src/context/context.c)
- [device.h](file://src/device/device.h)
- [device.c](file://src/device/device.c)
- [backend.h](file://src/backend/backend.h)
- [backend.c](file://src/backend/backend.c)
- [memory.h](file://src/memory/memory.h)
- [memory.c](file://src/memory/memory.c)
- [stream.h](file://src/stream/stream.h)
- [stream.c](file://src/stream/stream.c)
- [executor.h](file://src/executor/executor.h)
- [executor.c](file://src/executor/executor.c)
- [graph.h](file://src/graph/graph.h)
- [graph.c](file://src/graph/graph.c)
- [model.h](file://src/model/model.h)
- [model.c](file://src/model/model.c)

## Detailed Component Analysis

### Context Lifecycle
- Creation: Initializes backend, selects device, configures memory pool, creates default stream, sets up executor and graph registry. Errors during any step should abort creation and return an error code.
- Configuration: After creation, you can adjust settings such as default stream, logging level, or profiler toggles. Some configurations may require re-initialization of dependent components.
- Destruction: Releases memory, tears down backend, destroys stream(s), clears graph registry, and resets device state. Ensure no outstanding tasks remain on the stream before teardown.

```mermaid
flowchart TD
Start(["Create Context"]) --> InitBackend["Initialize Backend"]
InitBackend --> SelectDevice["Select Device"]
SelectDevice --> SetupMemory["Configure Memory Pool"]
SetupMemory --> CreateStream["Create Default Stream"]
CreateStream --> SetupExecutor["Setup Executor"]
SetupExecutor --> SetupGraphReg["Setup Graph Registry"]
SetupGraphReg --> Ready(["Context Ready"])
Ready --> DestroyStart(["Destroy Context"])
DestroyStart --> SyncStream["Sync/Clear Stream"]
SyncStream --> TeardownExecutor["Teardown Executor"]
TeardownExecutor --> ClearGraphReg["Clear Graph Registry"]
ClearGraphReg --> FreeMemory["Free Memory Pool"]
FreeMemory --> TeardownBackend["Teardown Backend"]
TeardownBackend --> End(["Destroyed"])
```

**Diagram sources**
- [context.c](file://src/context/context.c)
- [backend.c](file://src/backend/backend.c)
- [device.c](file://src/device/device.c)
- [memory.c](file://src/memory/memory.c)
- [stream.c](file://src/stream/stream.c)
- [executor.c](file://src/executor/executor.c)
- [graph.c](file://src/graph/graph.c)

**Section sources**
- [context.c](file://src/context/context.c)
- [context_test.c](file://src/context/context_test.c)

### Context and Devices
- Device selection determines where computations run (CPU/GPU/Metal). The context holds a reference to the active device and uses it for allocation and kernel dispatch.
- Device capabilities influence backend behavior and available optimizations.

```mermaid
sequenceDiagram
participant App as "Application"
participant Ctx as "Context"
participant Dev as "Device"
participant Mem as "MemoryPool"
App->>Ctx : create(config)
Ctx->>Dev : select(device_id)
Dev-->>Ctx : device handle
Ctx->>Mem : configure_for_device(device)
Mem-->>Ctx : ready
Ctx-->>App : context handle
```

**Diagram sources**
- [context.c](file://src/context/context.c)
- [device.c](file://src/device/device.c)
- [memory.c](file://src/memory/memory.c)

**Section sources**
- [device.h](file://src/device/device.h)
- [device.c](file://src/device/device.c)
- [memory.h](file://src/memory/memory.h)
- [memory.c](file://src/memory/memory.c)

### Context and Memory Pools
- The context configures a memory pool tailored to the selected device and backend. Allocation policies (e.g., fragmentation control, caching) are set at context creation time.
- All tensors and intermediate buffers within the context typically draw from this pool to ensure coherency and performance.

```mermaid
flowchart TD
A["Context Created"] --> B["Choose Memory Policy"]
B --> C{"Policy Supports Caching?"}
C --> |Yes| D["Enable Cache"]
C --> |No| E["Disable Cache"]
D --> F["Allocate Buffers"]
E --> F
F --> G["Return Allocations"]
```

**Diagram sources**
- [context.c](file://src/context/context.c)
- [memory.c](file://src/memory/memory.c)

**Section sources**
- [memory.h](file://src/memory/memory.h)
- [memory.c](file://src/memory/memory.c)

### Context and Backends
- The backend abstracts platform-specific implementations (CPU, CUDA, Metal). The context initializes the chosen backend and routes kernel calls through it.
- Backend lifecycle is tied to the context: when the context is destroyed, the backend is torn down.

```mermaid
sequenceDiagram
participant Ctx as "Context"
participant BE as "Backend"
participant Dev as "Device"
Ctx->>BE : init(target_device)
BE->>Dev : probe_capabilities()
Dev-->>BE : capabilities
BE-->>Ctx : initialized
Ctx->>BE : dispatch(kernel, args)
BE-->>Ctx : completion status
```

**Diagram sources**
- [context.c](file://src/context/context.c)
- [backend.c](file://src/backend/backend.c)
- [device.c](file://src/device/device.c)

**Section sources**
- [backend.h](file://src/backend/backend.h)
- [backend.c](file://src/backend/backend.c)

### Context and Streams
- A context provides a default stream for asynchronous execution. Tasks submitted to the stream are executed by the executor using the context’s backend and device.
- Synchronization points ensure ordering and safe teardown.

```mermaid
sequenceDiagram
participant Ctx as "Context"
participant Str as "Stream"
participant Exec as "Executor"
participant BE as "Backend"
Ctx->>Str : create_default()
Ctx->>Exec : get_executor()
Str->>Exec : submit(task)
Exec->>BE : execute(kernel)
BE-->>Exec : done
Exec-->>Str : task complete
Ctx->>Str : sync()
```

**Diagram sources**
- [context.c](file://src/context/context.c)
- [stream.c](file://src/stream/stream.c)
- [executor.c](file://src/executor/executor.c)
- [backend.c](file://src/backend/backend.c)

**Section sources**
- [stream.h](file://src/stream/stream.h)
- [stream.c](file://src/stream/stream.c)
- [executor.h](file://src/executor/executor.h)
- [executor.c](file://src/executor/executor.c)

### Context and Graphs
- Graphs represent compiled computation plans. The context hosts a graph registry scoped to itself, ensuring isolation between contexts.
- Models often compile or cache graphs within the owning context.

```mermaid
sequenceDiagram
participant App as "Application"
participant Ctx as "Context"
participant Reg as "GraphRegistry"
participant Mod as "Model"
App->>Ctx : create()
App->>Mod : load(model_path)
Mod->>Ctx : register_graph("model_v1", graph)
Ctx->>Reg : add(name, graph)
App->>Ctx : run_inference()
Ctx->>Reg : find("model_v1")
Reg-->>Ctx : graph
Ctx-->>App : results
```

**Diagram sources**
- [context.c](file://src/context/context.c)
- [graph.c](file://src/graph/graph.c)
- [model.c](file://src/model/model.c)

**Section sources**
- [graph.h](file://src/graph/graph.h)
- [graph.c](file://src/graph/graph.c)
- [model.h](file://src/model/model.h)
- [model.c](file://src/model/model.c)

### Context and Models
- Models depend on the context’s device, memory, and backend for loading weights and executing inference.
- Best practice: keep model instances bound to the same context that loaded them.

```mermaid
sequenceDiagram
participant App as "Application"
participant Ctx as "Context"
participant Mod as "Model"
participant BE as "Backend"
participant Mem as "MemoryPool"
App->>Ctx : create()
App->>Mod : new(context)
Mod->>Mem : alloc(weights)
Mod->>BE : prepare_kernels()
App->>Mod : infer(inputs)
Mod->>BE : execute()
BE-->>Mod : outputs
Mod-->>App : predictions
```

**Diagram sources**
- [context.c](file://src/context/context.c)
- [model.c](file://src/model/model.c)
- [backend.c](file://src/backend/backend.c)
- [memory.c](file://src/memory/memory.c)

**Section sources**
- [model.h](file://src/model/model.h)
- [model.c](file://src/model/model.c)

## Dependency Analysis
The context depends on several subsystems. The following diagram summarizes direct dependencies and their roles.

```mermaid
graph LR
Ctx["Context"] --> Dev["Device"]
Ctx --> BE["Backend"]
Ctx --> Mem["MemoryPool"]
Ctx --> Str["Stream"]
Ctx --> Exec["Executor"]
Ctx --> GR["GraphRegistry"]
Mod["Model"] --> Ctx
Gr["Graph"] --> Ctx
```

**Diagram sources**
- [context.h](file://src/context/context.h)
- [context.c](file://src/context/context.c)
- [device.h](file://src/device/device.h)
- [backend.h](file://src/backend/backend.h)
- [memory.h](file://src/memory/memory.h)
- [stream.h](file://src/stream/stream.h)
- [executor.h](file://src/executor/executor.h)
- [graph.h](file://src/graph/graph.h)
- [model.h](file://src/model/model.h)

**Section sources**
- [context.h](file://src/context/context.h)
- [context.c](file://src/context/context.c)

## Performance Considerations
- Prefer one context per device to avoid cross-device transfers and synchronization overhead.
- Reuse contexts across multiple inference runs to amortize backend and graph setup costs.
- Tune memory pool policies based on workload characteristics (e.g., enable caching for repeated shapes).
- Use streams judiciously: batch submissions and explicit sync only when necessary.
- Keep model graphs cached within the context to avoid recompilation.

[No sources needed since this section provides general guidance]

## Troubleshooting Guide
Common issues and resolutions:
- Initialization failures: Check backend availability and device capability probing. Validate configuration parameters passed to context creation.
- Out-of-memory errors: Inspect memory pool stats and reduce batch sizes or enable more aggressive caching if supported.
- Deadlocks or hangs: Ensure streams are synchronized before destroying contexts; verify no pending tasks remain.
- Cross-context misuse: Do not share tensors, graphs, or models across contexts without explicit migration; bind objects to the correct context.

Operational checks:
- Verify context creation returns success and that device/backend are initialized.
- Confirm stream sync completes before teardown.
- Validate graph registry entries exist when running inference.

**Section sources**
- [context_test.c](file://src/context/context_test.c)
- [context.c](file://src/context/context.c)

## Conclusion
Hummingbird’s Context Management provides a robust abstraction for isolating execution environments, managing resources, and coordinating devices, memory, backends, streams, executors, graphs, and models. By adhering to proper lifecycle practices, scoping resources to contexts, and tuning configuration for your workload, you can achieve predictable performance and reliable operation across single- and multi-context scenarios.

[No sources needed since this section summarizes without analyzing specific files]

## Appendices

### Practical Examples and Patterns
- Single-context initialization:
  - Create a context with a CPU device and default memory policy.
  - Load a model into the context and run inference.
  - Destroy the context after use.
- Multi-context scenarios:
  - Create separate contexts for different devices (e.g., CPU and GPU).
  - Bind models to their respective contexts and run concurrently.
  - Ensure each context is destroyed independently.
- Proper cleanup:
  - Synchronize streams, clear graphs, free memory, and tear down backend in reverse order of initialization.

[No sources needed since this section provides conceptual examples]