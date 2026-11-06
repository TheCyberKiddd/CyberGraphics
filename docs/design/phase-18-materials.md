# Phase 18 design — materials and instances

Date: 2026-11-05
Status: complete
Related: [Phase 18 work package](../graphics/CYBERGRAPHICS_BUILD_GUIDE.md#phase-18-material-system), [ADR 0021](../adr/0021-material-definitions-and-parameter-blocks.md), [material format](../material-format.md)

## Goal

Add a renderer-owned material system that separates immutable material
definitions from mutable instances, validates named parameters against shader
reflection, binds parameter blocks and texture slots through the RHI, sorts
draw records deterministically, and serializes definitions in a versioned text
format. Demonstrate 100 instances, sorted opaque/masked/transparent queues, and
a visible parameter change at the next frame boundary.

## Scope and model

- `MaterialDefinition` owns a graphics pipeline reference, material model,
  render-state/queue metadata, parameter schemas (type, default, component
  ranges), and texture-slot schemas.
- `MaterialInstance` refers to one definition, stores only parameter/texture
  overrides, tracks a monotonically increasing content revision, and exposes a
  resolved parameter block in the reflected constant-member order.
- Initial material model is glTF 2.0 metallic-roughness. Preserve the glTF
  factor defaults and ranges; identify base-color values as linear factors,
  metallic and roughness as normalized factors, and texture color-space roles.
  This phase defines and transports material data; the engine's full lighting
  and PBR renderer remain later work.
- Parameter schemas must match reflected member names and types. Defaults and
  instance changes must be finite and inside their declared per-component
  ranges. Unsupported reflected members fail definition creation explicitly.
- Text definitions store paths/URIs, never process-local RHI handles. A caller
  resolves texture asset URIs to live texture views and supplies them when
  creating or updating an instance. CyberGraphics does not add an asset loader
  or dependency in this phase.

## GPU data and ownership

- Extend reflected layout construction to route the compiler's `b0, space0`
  block to the existing push-constant path and one material constant buffer at
  `b1, space2` to `BindGroupSlot::PerMaterial` as a uniform-buffer binding.
  RHI layouts retain explicit shader-register indices so D3D12 root-signature
  ranges preserve resource class and register identity. Reject collisions,
  unsupported registers/types, bad sizes, and missing reflection matches
  before creating RHI objects.
- Each material instance owns a retained uniform buffer and a host-visible
  staging buffer for its parameter block. `RecordUpload` writes dirty staging
  bytes and records explicit `CopyDestination` transitions, copies, and
  return-to-`ConstantBuffer` transitions on a caller-provided graphics-capable
  command list. A D3D12 copy queue cannot perform the final shader-read
  transition, so the upload and consuming graphics work must share an ordered
  queue or explicit fence handoff. Repeated updates keep the staging buffer in
  `CopySource` and transition the uniform buffer from `ConstantBuffer` back to
  `CopyDestination`; dirty state clears only after the complete upload sequence
  is recorded. A bind group references the device-local buffer and resolved
  texture views.
- The caller controls submission and fence lifetime: it must not record a
  parameter upload to a buffer still read by an in-flight frame and must submit
  upload work before the graphics work that consumes it. This conservative
  per-instance strategy is correct but intentionally not optimized for large
  draw counts; Phase 19 owns the per-frame ring and dynamic offsets.
- Material instances borrow the RHI device, definition, and allocator; the
  allocator backs their CPU parameter bytes and must outlive the instance. RHI
  handles for resolved textures are borrowed and are never destroyed by
  materials.

## Draw ordering

Represent a draw as a value containing queue, pipeline identity, material
identity, positive forward camera distance (`-viewPosition.z`), and a stable
submission sequence. Sort by queue first; opaque and masked draws group by
pipeline then material and go front-to-back; transparent draws sort back-to-
front before pipeline/material tie-breaks. The camera convention is
right-handed; the sort API accepts explicit finite depth and does not infer a
view transform.

Use deterministic in-place sorting with no allocation. Reject invalid queue
values and negative or non-finite depth when building keys; stable sequence is
the final tie-breaker. Tests cover queue precedence, opaque/masked grouping,
reverse-Z independence, transparent depth order, equal-key stability, and 100
material instances.

## Text format and hostile input

The normative `.cybermat` grammar is in `docs/material-format.md`. It is a
small line-oriented UTF-8 format rather than JSON so Phase 18 does not create a
second general-purpose JSON parser ahead of the asset-parser phase. Require
magic/version, bounded names and records, finite numeric values, duplicate
checks, explicit `end`, and rejection of trailing non-comment data. Parsing and
writing use fixed-size caller-owned values and output spans, never allocate,
and do not depend on RHI state.

## Validation and done-when evidence

- Unit tests cover definition/instance validation, defaults/overrides/ranges,
  texture-slot resolution, reflection offsets, serialization round trips,
  malformed/truncated/oversized input, and sort-key ordering.
- Null-backend tests create parameter buffers and bind groups, record uploads,
  verify transitions and binding, and check that dirty revisions clear only
  after a successful recorded update.
- A D3D12 `07_materials` sample creates 100 material instances, draws them in
  the sorted order, changes one parameter while running, and demonstrates the
  changed instance on the next submitted frame. The sample reports submission
  order, parameter revision, and the changed frame; capture/render evidence
  belongs in the report.
- Run Debug, Release, and Debug-ASan tests, structure/layer/format checks, and
  the package consumer after the public renderer API is added. Do not mark the
  phase complete or tick the roadmap until the sample and validation evidence
  exist.

## Sources

- Khronos, [glTF 2.0 specification — Materials](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials).
- Khronos, [glTF 2.0 material JSON schema](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.schema.json).
- Google Filament, [Materials Guide](https://google.github.io/filament/Materials.html) — conceptual reference only; no source code is copied.
