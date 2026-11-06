# Phase 23 design: geometry pipeline

## Goal and scope

Add validated CPU mesh storage and geometry processing to `cyber_scene`: a
full-float vertex format, indexed submeshes and bounds, tangent generation,
compact vertex/index encodings, deterministic mesh simplification with a
measured geometric error, and instance batches that can be consumed as
indirect indexed draws. Validate the result against reference calculations and
exercise visible LOD changes with a large instanced scene.

This phase does not add source-asset parsing, cooked assets, GPU resource
ownership, backend calls, skinning evaluation, or a new renderer queue. Those
remain in the asset and renderer phases. `cyber_scene` owns CPU geometry and
emits backend-neutral batch data; the renderer remains responsible for creating
RHI buffers and issuing supported draws.

## Module boundary and ownership

- Keep geometry in `cyber_scene`, whose Phase 22 contract already owns copied
  mesh-instance proxies and explicitly assigns geometry processing to Phase
  23. This avoids introducing a one-purpose module before the asset pipeline
  exists and keeps CPU geometry available to renderer and offline consumers.
- Mesh storage is allocator-backed and move-only. Input spans are copied and
  validated; no caller pointer is retained. A mesh's vertex, index, and
  submesh views remain stable only for that mesh's lifetime and are invalidated
  by mutation.
- Geometry algorithms are pure CPU operations and have no hidden state,
  threads, GPU access, or global allocation policy.
- Index data is 32-bit in the canonical CPU representation. Packing chooses
  16-bit indices when every referenced vertex fits and 32-bit otherwise.
- Full-float vertices are the correctness reference. Packed encodings are
  explicit conversions and must report invalid input or capacity errors rather
  than silently substituting data.

## Geometry rules

- A triangle mesh has finite attributes, in-range indices, non-empty
  triangle-aligned submesh ranges, and finite non-empty bounds computed from
  positions. Submesh ranges preserve material boundaries through simplification
  and draw batching.
- Tangents are accumulated from indexed triangle positions and UV0, then
  orthogonalized against normalized normals. The tangent `w` is `+1` or `-1`
  and follows the repository convention
  `bitangent = cross(normal, tangent) * w`. Degenerate geometry/UVs have a
  documented deterministic fallback; non-finite or unusable normals are
  rejected.
- LOD simplification uses an in-house edge-collapse method. It does not collapse
  across submesh/material boundaries or flip/invert surviving faces. Each
  output reports its triangle count and the maximum displacement of each
  referenced source vertex from its resulting representative vertex. This
  correspondence error is not a full surface Hausdorff bound. The simplifier
  stops rather than claiming a requested target if safe collapses are exhausted.
- Instance batches are deterministic and partition by mesh, submesh/material,
  and selected LOD. Each batch carries backend-neutral indexed-draw arguments
  with checked 32-bit counts and a separate contiguous instance range.
  Capability checks, buffer creation, synchronization, and actual indirect
  submission stay in `cyber_renderer` and `cyber_rhi`.

## Allocation and failure behavior

Every owner receives a caller-owned allocator. Constructors reserve no hidden
global memory. All count, byte-size, index, and packed-format conversions are
checked before allocation or narrowing. Invalid mesh topology, non-finite
attributes, invalid tolerances, overflow, exhausted capacity, and allocation
failure return the repository's explicit `Error` values. Algorithms leave
their input unchanged on failure.

## Verification

Tests cover public-header self-containment, mesh validation and ownership,
bounds, submesh and index-format selection, tangent orthogonality and
handedness, packed-format round trips and error bounds, simplification
topology/material preservation and measured error, deterministic batching,
overflow, and allocation failure. A reference geometry and a million-triangle
instanced workload verify culling/batching against an independent CPU result;
the visual acceptance path records the selected LODs and shows a visible
transition rather than treating a proxy count as a render.

## Reading notes

The [Phase 23 reading notes](../notes/phase-23.md) record the relevant glTF
vertex/tangent conventions and Direct3D 12 indirect-argument behavior. They are
used as format and API references only; no external implementation code is
copied.
