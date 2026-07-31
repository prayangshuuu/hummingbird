---
kind: external_dependency
name: CUDA Backend Accelerator
slug: cuda-toolkit
category: external_dependency
category_hints:
    - client_constraint
scope:
    - '**'
---

Optional GPU backend built only when HB_BACKEND_CUDA=ON. Requires CUDA toolkit installation. Currently scaffold-only (registration scaffolding), actual kernels arrive in milestone M7. Built as separate static library hb_backend_cuda with CUDA_STANDARD 17.