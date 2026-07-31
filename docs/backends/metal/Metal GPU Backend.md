---
kind: external_dependency
name: Metal GPU Backend
slug: metal-framework
category: external_dependency
category_hints:
    - client_constraint
scope:
    - '**'
---

Optional Apple Silicon backend built only when HB_BACKEND_METAL=ON on macOS. Links against Metal and Foundation frameworks. Currently scaffold-only registration layer; actual kernels planned for milestone M7.