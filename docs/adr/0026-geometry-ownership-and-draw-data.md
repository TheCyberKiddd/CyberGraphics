# 0026 — Geometry ownership and draw-data boundary

- **Status:** accepted
- **Date:** 2026-11-06
- **Deciders:** @TheCyberKiddd
- **Related:** phase 23, [design note](../design/phase-23-geometry.md),
  ADR 0025

## Context

Phase 23 adds CPU mesh storage and processing, LOD generation, and instanced
indirect draw data. The existing `cyber_scene` module owns renderer-facing
mesh-instance proxies, has an explicit Phase 23 geometry-processing roadmap,
and must not depend on the higher-layer renderer. `cyber_rhi` already defines
backend-neutral indexed indirect arguments and capability-gated submission.

## Decision

Keep canonical mesh data, tangent/bounds processing, simplification, packed
representations, and deterministic instance-batch construction in
`cyber_scene`. Meshes own allocator-backed copies and use 32-bit canonical
indices; explicit packing selects 16-bit indices only when safe. Instance
batches expose backend-neutral indexed-draw arguments and instance ranges.
GPU buffers, indirect capability negotiation, submission, and lifetime/fence
management remain in `cyber_renderer`/`cyber_rhi`. Source import and cooked
asset identity remain Phase 24 work.

## Consequences

### Positive

- Reuses the existing scene module and its ownership rules without a new
  dependency or duplicate scene/geometry registry.
- Keeps algorithms testable on the CPU and independent of adapter availability.
- Provides a clear, stable boundary between CPU geometry preparation and
  backend-specific resource ownership.
- Preserves material/submesh identity and makes index narrowing explicit.

### Negative

- Consumers that need GPU geometry must upload the produced views and manage
  their GPU lifetime in the renderer.
- Canonical 32-bit CPU indices use more memory than the compact representation;
  packing is an explicit build/upload step.
- The first simplifier may preserve boundaries conservatively and therefore
  miss a requested triangle target on meshes with limited collapsible edges.

### Neutral

- This ADR does not promise that every RHI backend supports indirect drawing;
  callers must use the existing capability query and a direct-draw fallback.
- Packed vertex layouts and indirect argument records are runtime conversion
  formats, not the Phase 24 persistent cooked-asset format.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Put mesh generation in `cyber_renderer` | Couples CPU geometry algorithms to GPU policy and prevents the scene module from owning its declared Phase 23 work. |
| Create a new `cyber_geometry` module | Adds a module and dependency-order changes for one phase without an existing independent consumer or asset boundary. |
| Make `cyber_scene` depend on `cyber_rhi` | Reverses the intended layer boundary and gives scene data responsibility for GPU capabilities and fences. |
| Keep only expanded software-renderer triangles | Loses indexed sharing, submesh boundaries, and a backend-neutral indirect draw description. |

## Follow-up

Implement and validate the CPU geometry pipeline in `cyber_scene`; consume its
draw data from the renderer only where the existing RHI capabilities permit.
Revisit module ownership when Phase 24 establishes the cooked mesh and asset
database contracts.
