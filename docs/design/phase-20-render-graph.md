# Phase 20 design: render graph

Date: 2026-11-06
Status: implemented
Related: [ADR 0023](../adr/0023-render-graph-lifetimes-and-aliasing.md)

## Scope

Add a `cyber_renderer` graph that declares pass resource usage before command
recording, derives dependencies and liveness, allocates compatible transient
resources with non-overlapping lifetimes into shared physical slots, inserts
RHI transitions, and exports human-readable and Graphviz DOT diagnostics.
The demonstration is a depth prepass, main pass, and post-processing pass with
an unused branch.

The graph does not add a render thread, cross-queue scheduling, implicit pass
callbacks that own RHI objects, dynamic resolution, or a plugin ABI. Passes are
recorded in deterministic declaration order on a caller-owned command list.
The caller owns imported resources and their final destruction; the graph owns
the transient physical resources it creates.

## Public contract

- `RenderGraph` is created with an explicit `Device` and allocator; it is
  neither copyable nor movable so resource handles retain a stable graph owner.
- Setup callbacks run once when a pass is added and may only declare resource
  reads and writes. Side-effect roots are part of the pass description. Execute
  callbacks run only during graph execution with a pass context resolving
  logical resources to RHI handles and the active command list.
- Graph resources are graph-owned, generation-checked handles. Imported
  buffers and textures capture their current RHI-tracked state at import;
  transient resources begin undefined and must be written before they are read.
- Resource hazards are inferred in declaration order. Exported resources and
  side-effecting passes root liveness; dependencies of live passes remain live.
- Transient resources alias only when their complete RHI descriptors match and
  their live pass intervals are disjoint. The shared RHI allocation is marked
  `MemoryLifetime::Transient` so the Phase 15 allocator can safely retire it.
- Before a pass executes, the graph records required buffer and texture
  barriers on the supplied recording command list. It does not submit or
  destroy imported resources. The caller must acknowledge a successful
  submission before reusing the graph; discarding its command list invalidates
  that graph instance for reuse. The caller discards unsubmitted lists and
  ensures submitted work no longer references graph-owned transients before
  destroying the graph.
- Text and DOT exports write into caller-owned bounded storage and report
  insufficient capacity instead of truncating.

## Failure behavior

Invalid handles, incompatible usage/state declarations, reading an unwritten
transient resource, malformed pass setup, allocation failure, invalid command
list state, barrier failure, and export-buffer exhaustion are explicit errors.
A failed recording is not rolled back; the command list and graph must be
discarded before reuse.

## Validation

Unit tests cover public-header self-containment, handle validation, setup and
execution order, hazard dependencies including a diamond, dead-pass culling,
transient alias eligibility and overlap exclusion, imported resource state,
automatic transitions, and bounded text/DOT output. The demonstration test
records a three-pass graph on the null backend, checks the expected live and
culled pass counts and transitions, then writes a DOT dump below the build
tree.

## Source

The build guide lists the GDC 2017 Frostbite FrameGraph talk and slides. The
Vault page required a member login during implementation; see
[`docs/notes/phase-20.md`](../notes/phase-20.md). The design follows the
project's own RHI, resource-lifetime, and threading contracts and uses no
third-party code.
