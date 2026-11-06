# 0021 — Material definitions, instances, and upload ownership

- **Status:** accepted for Phase 18
- **Date:** 2026-11-05
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 18, [design note](../design/phase-18-materials.md),
  [material format](../material-format.md), ADR 0020

## Context

The Phase 18 material system needs definitions separate from instance
overrides, reflected parameter blocks, texture slots, serialized metadata, and
sorted draws. `cyber_renderer` has reflection and pipeline creation but does
not yet expose material ownership. The RHI can create uniform buffers and bind
groups, while CPU-writable staging buffers must be copied into device-local
uniform buffers. Per-frame streaming and dynamic offsets are intentionally
scheduled for Phase 19.

## Decision

1. Keep definitions immutable and instances mutable. A definition owns
   parameter metadata (type/default/range), render queue/state, pipeline
   association, and texture-slot URIs. Instances store overrides and resolved
   texture views; texture resources remain caller-owned.
2. Derive parameter byte offsets and uniform bindings from `cyber_shaders`
   reflection. The initial renderer contract reserves `b1, space2` for one
   material parameter block, keeping `b0, space0` as push constants. Carry
   explicit shader-register indices in RHI bind-group layouts so D3D12 root
   signature ranges preserve CBV/SRV/UAV/sampler classes and reflected register
   locations. Validate definition schemas against reflected member names,
   types, defaults, and ranges; do not duplicate shader-layout knowledge in
   authored material files.
3. Use per-instance retained uniform buffers with host-visible staging buffers
   and explicit RHI copy/barrier recording for dirty material parameters.
   Record uploads on a graphics-capable command list; a D3D12 copy queue cannot
   transition the destination to a shader-readable constant-buffer state.
   Repeated updates transition the retained uniform from
   `ConstantBuffer` through `CopyDestination`, while staging remains
   `CopySource`. Callers own command submission and fence-safe update timing.
   Phase 19 may replace this conservative transport with a per-frame ring and
   dynamic offsets without changing material semantics.
4. Introduce `.cybermat`, a bounded line-oriented versioned UTF-8 format. Do not
   use JSON or add a dependency before Phase 24's parser work; reject malformed,
   truncated, oversized, duplicate, or non-finite input with source locations.
5. Sort queue first; opaque/masked draws front-to-back and group by pipeline
   and material; transparent draws back-to-front. Use deterministic ties and
   caller-provided depth rather than hidden camera/global state.
6. Represent glTF metallic-roughness parameters and texture roles in Phase 18,
   without claiming to implement the full production lighting model; physically
   based rendering and lighting are Phase 26.

## Consequences

### Positive

- Shader reflection, runtime parameter blocks, and material authorship share
  one layout contract.
- Definitions can be shared across many lightweight instances, and sort order
  reduces pipeline/material changes for opaque draws.
- Per-instance uploads are simple to validate with the current RHI and do not
  preempt Phase 19's measured streaming work.
- A narrow text grammar meets the authoring/future-editor requirement without
  committing to an unneeded JSON implementation.

### Negative

- Per-instance staging and uniform buffers consume more resources than a ring
  buffer and require the caller to schedule safe uploads.
- Material texture URIs need a future asset resolver; the parser never opens
  referenced paths.
- Phase 18's metallic-roughness model defines and transports material
  properties but does not deliver the full lit renderer.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Put material code in `cyber_shaders` | Materials combine reflection with RHI state/resources; compiler must remain GPU-independent. |
| Serialize RHI handles | Handles are process/device-local and cannot survive restart or be authored. |
| Add a JSON dependency/parser | No runtime third-party libraries; generic JSON/glTF parsing belongs to Phase 24. |
| Upload every instance through one dynamic ring now | Phase 19 owns ring-buffer alignment, frame lifetime, and draw-count measurements. |
| Make every parameter an independent constant | Creates unstable layouts and per-field update overhead; use reflected packed blocks. |

## Follow-up

- Phase 19 replaces or layers a ring-buffer transport beneath stable parameter
  semantics.
- Phase 24 resolves texture URIs and material assets during import/cooking.
- Phase 26 implements and validates the full PBR lighting response.
