# 0015 — Four binding slots, location-based vertex input, and hash-keyed pipelines

- **Status:** accepted
- **Date:** 2026-10-26
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 12, [design note](../design/phase-12-pipelines-binding.md),
  ADR 0013, ADR 0014

## Context

Every D3D12 root signature, Vulkan descriptor set layout, and WebGPU bind group
layout encodes the same information in a different shape. The renderer needs one
model that none of them forces, that a backend can translate mechanically, and
that can be hashed so a pipeline cache means something.

Vertex input has the same problem in miniature: Vulkan and WebGPU address inputs
by location, D3D12 by semantic name and index.

## Decision

1. **Four binding slots, fixed by change frequency:** `PerFrame`, `PerView`,
   `PerMaterial`, `PerDraw`. A pipeline layout holds one group layout per slot
   and the slot order is the layout order. Four is a design choice, not a
   backend limit: it matches the null backend's `maxBindGroups`, D3D12's practical
   root-parameter budget, and Vulkan's four-set convention.

2. **Group layouts declare `{ binding, type, visibility, arrayCount }`; groups
   carry resources flattened in binding order.** A layout with array counts
   `1, 3, 1` takes five resources. Flattening keeps both arrays walkable together
   and removes a per-binding descriptor object from the ABI.

3. **Textures and samplers are separate binding types.** A combined image
   sampler is one API's convenience; modelling it would force one backend to
   split descriptors at creation and the other to merge at bind time.

4. **Vertex inputs are addressed by location, not semantic name.** The canonical
   mapping to D3D12 is `location N` → `TEXCOORDN`, which the Phase 16 HLSL
   generator will also emit. A backend that needs anything else maps it in one
   table.

5. **Push constants are up to four `{ offset, size, stages }` ranges**, bounded by
   `Limits::maxPushConstantBytes`. That covers D3D12's single root-constant bank
   and Vulkan's push-constant block with one shape.

6. **Descriptions are plain aggregates with a stable 64-bit hash.** Shader
   handles, formats, states, and span contents are hashed; labels and entry-point
   text are not, because the shader handle already identifies the code.

7. **The pipeline cache is an object**, created by the caller and passed into a
   pipeline description. It is a cache of compiled data, not a deduplicating
   handle table: two creations still produce two independent handles, so
   destroying one never invalidates the other.

8. **Validation is the null backend's, and it is exhaustive.** Every pipeline and
   group rule is a check with one static message, so a D3D12 or Vulkan backend
   can be measured against the same suite.

## Alternatives considered

- **An arbitrary number of groups.** Rejected: Vulkan allows 32 sets and D3D12
  effectively fewer root parameters; a renderer that permits more than four will
  still use four, and the extra generality buys a dynamic array in every layout.
- **A combined image sampler type.** Rejected for the reason in decision 3.
- **Semantic-name vertex inputs.** Rejected: it forces every caller to write HLSL
  semantics and makes a SPIR-V shader's locations a second concept.
- **One push-constant block.** Rejected: a vertex stage needs the object
  transform and a pixel stage the material constants; ranges with visible stages
  express that without wasting one block per stage.
- **A deduplicating pipeline cache that returns one handle.** Rejected: sharing a
  handle means a destroy in one owner invalidates another, which is exactly the
  aliasing ADR 0006 forbids.
- **Descriptions with pointers to arrays of owning objects.** Rejected: spans over
  plain data are hashable, comparable, and serializable, which the cache needs.

## Consequences

- A backend implements seventeen more table entries and one hash function pair.
- `PipelineLayoutHandle` and `PipelineCacheHandle` join the handle set; the device
  validates the whole table at creation, as Phase 11 established.
- A caller must write vertex attributes using locations that match shader inputs;
  the mismatch surfaces at draw time in Phase 13 and at compile time once the
  Phase 16 compiler emits both sides.
- The null backend stores descriptors (copying their arrays) so a bind group can
  be validated against its layout, and validates a pipeline's whole description
  at creation rather than at first use.
