# Hierarchical Streaming Engine & Adaptive Cache

This module implements the core streaming and memory hierarchy for the Hummingbird project (Milestone M4).

## Architecture

Conforms to the architecture laid out in `.claude/PROJECT_CONTEXT.md` for the streaming engine and cache hierarchy.

### Components

1. **Hierarchical Memory Manager**: Handles allocation and migration across 4 tiers:
   - Tier 0: VRAM / Pin / Hot
   - Tier 1: RAM / Main Memory
   - Tier 2: NVMe / SSD / Disk
   - Tier 3: Remote / Cold / Archive

2. **Adaptive Cache Manager**: Manages cache eviction policies. Currently supports LRU, with LFU and ARC as pluggable options for future.

3. **Streaming Scheduler**: Orchestrates the movement of data between memory tiers. Supports Load, Unload, Promote, Demote, Pin, Unpin, Prefetch, Evict, Invalidate, and Sync.

4. **Prefetch Engine**: A framework to asynchronously issue prefetch commands for upcoming blocks based on router lookahead or heuristics.

5. **Locality Tracker**: Tracks statistics on hits, misses, promotions, demotions, and bytes transferred. Calculates a temporal locality score.

## Memory Hierarchy

The engine treats VRAM, RAM, and Disk as a single continuous memory hierarchy. Blocks are transparently moved between these tiers based on heat, pinning status, and eviction policies.

## Cache Policies

The Adaptive Cache Manager allows different eviction strategies to be plugged in:
- **LRU (Least Recently Used)**: Evicts the oldest accessed items. (Implemented)
- **LFU (Least Frequently Used)**: Evicts the least accessed items over time.
- **ARC (Adaptive Replacement Cache)**: Dynamically balances between recency and frequency.

## Residency Lifecycle

1. **Evicted / Not Present**: Block is not loaded.
2. **In-Transit**: Block is currently being loaded or moved between tiers.
3. **Resident**: Block is loaded in a memory tier (typically RAM or VRAM) and ready to use.
4. **Pinned**: Block is resident and locked in memory; it will not be evicted by standard policies.

## Benchmarks

Run the benchmarks with `benchmarks/bench_stream.c` to test sequential and random streaming load performance.

## Testing

The test suite in `stream_test.c` verifies memory tier allocations, cache eviction (LRU), and scheduler block promotion and demotion.
