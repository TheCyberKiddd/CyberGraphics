# 0022 — Frame streaming, dynamic offsets, and bindless tables

- **Status:** accepted for Phase 19
- **Date:** 2026-11-06
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 19, [design note](../design/phase-19-binding-streaming.md), ADR 0018, ADR 0021

## Context

Phase 18 deliberately gives each material instance retained uniform and staging
buffers. That is straightforward and safe for a small scene but scales poorly
to thousands of per-draw values. The RHI already exposes a fence-backed frame
ring, alignment limits, structured buffers, descriptor arrays, and explicit
resource transitions. It does not yet expose dynamic binding offsets, mutable
bind groups, or descriptor-index allocation. `Feature::BindlessResources` is
already part of the closed capability enum but is not yet reported by D3D12.

## Decision

1. Keep frame-slot lifetime explicit: the renderer stream consumes a
   `FrameSlot` only after `FrameRing::BeginFrame` has waited for the matching
   completion fence. Each slot owns a staging buffer and a GPU destination;
   bounded allocations are aligned from the device limits and recorded as
   explicit copies and barriers. No hidden waits or global frame state.
2. Make dynamic offsets opt-in per uniform, writable-storage, or read-only
   storage binding, limited to a single buffer resource per dynamic layout
   entry. Writable storage maps to a UAV; read-only storage maps to an SRV.
   Validate offset count, alignment, integer overflow, and effective range
   before recording. D3D12 represents these bindings as root descriptors;
   ordinary bindings remain descriptor tables. Reject root signatures that
   exceed the backend budget.
3. Keep descriptor allocation deterministic and independent of backend
   handles. Persistent entries use index-plus-generation handles and a free
   list. Transient entries are per-frame linear ranges that become reusable
   only when the corresponding frame slot is safe. Bindless array indices are
   these logical indices; table mutation updates existing per-frame bind groups
   after their fences complete.
4. Report `Feature::BindlessResources` only when the backend has verified the
   adapter's indexing requirements. Bindless tables are finite arrays populated
   with valid caller-provided fallback resources. Adapters without the feature
   use conventional per-draw bindings; that is a supported path, not a
   success-shaped approximation of bindless support.
5. Preserve Phase 18 material definition and instance semantics. Add a stream
   transport path without removing the retained-buffer path, so callers can
   adopt streaming incrementally and maintain low-volume compatibility.
6. Measure CPU work for the same 10,000 unique-transform draw workload on the
   streamed/bindless-capable path and the conventional fallback. Record
   methodology and measured distributions; do not claim a performance
   improvement without comparing measurements.

## Consequences

### Positive

- Per-draw data can share a bounded set of frame resources without overwriting
  in-flight GPU reads.
- Dynamic offsets express the renderer's frequency-of-change model directly
  while avoiding one descriptor allocation per draw on D3D12.
- Descriptor indices can be recycled safely, and bindless support is gated on
  a reported capability with a separately exercised conventional path.
- Existing material consumers can keep their current upload semantics.

### Negative

- Dynamic bindings consume D3D12 root-signature budget and have stricter
  validation than static descriptor-table bindings.
- Bindless tables require stable finite capacities and valid fallback
  resources; this phase does not provide asset URI resolution.
- Upload copies and table updates remain ordered by the caller's frame fence and
  queue submission.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Put all per-draw values in push constants | The portable push-constant limit is 128 bytes and is not an efficient object-data transport. |
| Allocate a bind group for every draw | It multiplies descriptor consumption and prevents bounded per-frame streaming. |
| Always enable bindless indexing | It would report support on adapters that do not provide the required indexing tier. |
| Rebuild descriptor tables every frame | It creates unbounded descriptor churn with the current fixed-size D3D12 heaps. |
| Remove Phase 18 per-instance buffers | It forces every caller to adopt a high-volume streaming model and changes compatibility unnecessarily. |

## Follow-up

Phase 19's measured capacity and CPU submission data inform the scene,
render-thread, render-graph, and GPU-driven phases. Asset URI resolution and
material import remain Phase 24 work.
