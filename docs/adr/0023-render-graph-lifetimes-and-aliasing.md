# 0023 — Render graph pass liveness and transient aliasing

- **Status:** proposed
- **Date:** 2026-11-06
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 20, [design note](../design/phase-20-render-graph.md),
  ADR 0014 (resource model), ADR 0016 (command recording), ADR 0018 (GPU heap
  sub-allocation and RHI validation), [`cyber_renderer` contract](../../engines/cyber_renderer/docs/MODULE.md)

## Context

Phases 11–15 provide explicit RHI resources, resource states, command-list
barriers, and a transient-capable GPU allocator. Phase 20 needs a deterministic
way to discover pass dependencies before recording, remove work with no
observable result, and reuse transient memory without weakening RHI lifetime
validation.

## Decision

1. A graph pass has a setup callback and an execute callback. Setup records
   reads, writes, and side effects before any command is recorded. Execute
   receives a pass context and records work only on the caller's command list.
2. Resource hazards are inferred in pass declaration order. A read depends on
   the latest preceding writer; a write depends on all preceding readers and
   writers since the prior write. This produces a stable DAG without an
   implicit scheduler or cross-queue synchronization.
3. Liveness is traced backward from exported resources and explicit side
   effects. A pass that contributes no dependency to a root is culled.
4. Transient logical resources may share one physical RHI resource only when
   their complete resource descriptions match and their live-pass intervals do
   not overlap. This avoids backend-specific placed-resource APIs while letting
   the existing Phase 15 allocator sub-allocate the shared resource.
5. The graph emits barriers in dependency order immediately before each live
   pass. It never submits commands, owns imported resources, or handles
   cross-queue fences.
6. Debug exports are deterministic bounded text and DOT output into caller
   storage; file I/O remains outside the renderer runtime.

## Consequences

### Positive

- Pass setup is complete before execution, enabling culling and lifetime
  analysis without recording speculative commands.
- Alias decisions are inspectable and rely on exact descriptor compatibility
  plus non-overlapping intervals, not a guessed byte-size match.
- Backends retain responsibility for actual memory placement and fence-safe
  reuse; the graph remains RHI-agnostic.
- Callers control file output, allocation, submission, and imported-resource
  ownership explicitly.

### Negative

- Declaration order defines execution order; independent pass reordering and
  async compute are deferred.
- Exact descriptor matching can miss safe alias opportunities between
  differently shaped resources with equal compatible allocation requirements.
- A graph is not a transaction: failure after recording begins can leave a
  partially recorded command list and updated RHI tracked states.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Backend-specific placed resources | It would leak D3D12/Vulkan placement rules into the renderer and bypass the Phase 15 allocator contract. |
| Alias resources by byte size alone | Equal byte size does not prove compatible format, usage, sample count, or alignment. |
| Execute setup and callbacks interleaved | Liveness and aliasing would be unknowable before recording and could not safely cull work. |
| Reorder passes automatically | The project has no multi-queue scheduler or synchronization model for independent async work yet. |
| Write graph files from runtime code | File I/O is outside the renderer contract; caller-provided bounded output keeps diagnostics portable and explicit. |

## References

- [FrameGraph: Extensible Rendering Architecture in Frostbite](https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in), GDC 2017. The talk page was accessible, but the full talk and slides required a member login during this phase.
- [Phase 20 reading notes](../notes/phase-20.md).
