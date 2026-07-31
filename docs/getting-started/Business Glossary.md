---
kind: business_term
name: Business Glossary
category: business_term
scope:
    - '**'
---

### Model Adapter Framework
- Definition：RFC-014 architecture component that translates model-specific architectures into Hummingbird's generic execution runtime through a pluggable vtable interface. Enables dense Transformers and sparse MoE models to share the same core without hardcoding any model-specific logic.
- Aliases：adapter framework、model adapter layer

### hbm format
- Definition：Hummingbird Model container format — a safetensors-compatible base with explicit tensor descriptors, documented grouping rules for streamable units, and model-descriptor metadata. Produced by converters, consumed by the model loader.
- Aliases：.hbm、Hummingbird Model format

### weight class
- Definition：Model-declared classification driving placement decisions (RESIDENT, STREAMED, SENSITIVE_F32) rather than hardcoded expert/dense assumptions. The memory manager places tensors by class + measured heat across VRAM/RAM/NVMe tiers.
- Aliases：weight class taxonomy、placement class

### learning cache
- Definition：Three-tier caching system (Pin/LRU/Disk) that persists usage patterns per-turn to improve performance over repeated workloads. Uses LFRU score with hysteresis for live-swapping between tiers.
- Aliases：expert learning cache、tiered cache

### PIPE scheduler
- Definition：Background I/O overlap mechanism using bounded pthread pool (default 8 workers) that loads misses while resident experts compute, joining before layer completion. Uses lock-free generation-tagged cursor with atomic ready flags.
- Aliases：I/O pipeline、async loader

### PILOT prefetcher
- Definition：Router-lookahead thread that applies layer L+1's router to layer L's post-attention state (71.6% top-8 recall) to prefetch experts ahead of time. Cross-layer handshake via mutex/condvar coordination.
- Aliases：router lookahead、prefetch thread

### MTP speculation
- Definition：Multi-Token Prediction speculative decoding: draft N tokens, verify all in one batched forward, accept matching prefix. Must be provably lossless with rejection sampling to maintain token-exact correctness.
- Aliases：speculative decoding、draft/verify

### Colibrì
- Definition：Reference implementation that inspired Hummingbird — a single-translation-unit C engine running GLM-5.2 (744B MoE) on ~25 GB RAM by streaming int4 experts from disk. Studied as source of hard-won design lessons.
- Aliases：reference engine、inspiration
