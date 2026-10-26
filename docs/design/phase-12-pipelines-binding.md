# Phase 12 — Pipelines and the binding model

Date: 2026-10-26
Status: approved
Related: ADR 0013 (RHI object model), ADR 0014 (resources and states),
ADR 0015 (pipeline and binding decisions),
[`cyber_rhi` contract](../../engines/cyber_rhi/docs/MODULE.md),
[Phase 11 note](phase-11-resources.md)

## What this phase builds

Phase 11 gave the renderer resources. Phase 12 gives it the fixed-function and
binding state that decides how those resources are consumed:

- **A shader module description**: stage, bytecode blob, and entry point, with
  the format tagged so a backend knows whether it reads HLSL source or SPIR-V.
- **A graphics pipeline description** made of plain structs: shaders, vertex
  layout, topology, rasterizer, multisample, depth-stencil, blend, color
  attachment formats, and an optional pipeline cache.
- **A compute pipeline description**: one shader and a layout.
- **A binding model**: group layouts (slots with a type, a count, and visible
  stages), bind groups (the actual resources), a pipeline layout that fixes the
  group order, and push-constant ranges for small per-draw constants.
- **A pipeline cache** keyed by a stable hash of the description.
- **Validation** that rejects the combinations no API can create: a missing
  shader, an attribute outside its buffer, a blend on an integer target, a depth
  format used as a color attachment, a mismatched sample count, a group whose
  resources do not match its layout.

Done-when: *every pipeline is described by plain structs and invalid
combinations are rejected with clear messages, covered by tests.*

## The binding model

Four group slots, fixed by change frequency. The number is not a backend limit;
it is a design choice that keeps descriptor updates cheap and makes the mapping
to D3D12 root signatures and Vulkan descriptor sets direct.

| Slot | Changes | Typical contents |
|---|---|---|
| `PerFrame` | Once per frame | Camera and time constants, the frame's light buffer |
| `PerView` | Once per view | A shadow atlas or a reflection probe set |
| `PerMaterial` | Once per material | Material constants, material textures and samplers |
| `PerDraw` | Once per draw | Object transform, per-instance data |

A pipeline layout names a group layout per slot, in slot order. A slot may be
empty, which is how a compute pipeline declares one group and no vertex state.

A group layout entry is `{ binding, type, visibility, arrayCount }`:

| Type | Resource in a group | D3D12 | Vulkan |
|---|---|---|---|
| `UniformBuffer` | A buffer range | CBV in a descriptor table | `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` |
| `StorageBuffer` | A buffer range | UAV | `STORAGE_BUFFER` |
| `SampledTexture` | A texture view | SRV | `SAMPLED_IMAGE` |
| `StorageTexture` | A texture view | UAV | `STORAGE_IMAGE` |
| `Sampler` | A sampler | Sampler in the table | `SAMPLER` |

Textures and samplers are separate bindings. A combined image sampler is a
convenience one API has and another does not; keeping them apart costs one extra
descriptor per material and keeps every backend's mapping mechanical.

`arrayCount` describes a binding that is indexed at run time. The resources
array in a bind group is flattened: a layout of `{ 0: 1, 1: 3, 2: 1 }` takes five
resources in binding order, so the caller writes them in the order the layout
declares and the backend can walk both arrays together.

### Push constants

`PushConstantRange { offsetBytes, sizeBytes, stages }` with at most four ranges,
which is one more than D3D12's single root-constant bank needs and exactly what
Vulkan allows comfortably. Ranges may not overlap within a stage. The total is
bounded by `Limits::maxPushConstantBytes` (128 in the null backend, which is the
D3D12 root-constant budget).

## Vertex input

An attribute names a `location`, a `binding`, a `Format`, and an offset. This is
the Vulkan and WebGPU shape. D3D12 wants a semantic name and index; the canonical
mapping is `location N` → `TEXCOORDN`, which the generated HLSL in Phase 16 will
also emit, so both sides agree without a table. A backend that needs a different
name maps it in one place.

The vertex format is restricted to the formats a vertex fetch can meaningfully
produce: color layout, not depth, not block compressed, not sRGB. A float, unorm,
or uint vertex attribute is accepted; a BC block or a depth format is rejected
with a message that names the attribute.

Validation also checks that `offset + formatSize <= stride` for the attribute's
binding, that no two attributes share a location, and that a binding's stride is
non-zero and at most 2048 bytes.

## Why the descriptions are hashable

Pipeline creation is expensive on real hardware — a D3D12 PSO compile or a
Vulkan pipeline compile is milliseconds — and a renderer creates the same
pipeline thousands of times across material permutations. The cache interface is
only useful if "the same pipeline" is well defined, so every pipeline
description has a stable 64-bit hash:

```cpp
u64 hash = HashGraphicsPipelineDesc(desc);
```

The hash covers every field that changes the compiled object: shader handles
(including generation, so a reloaded shader is a new pipeline), formats, states,
attribute spans, attachment spans, and the layout handle. It deliberately does
*not* cover labels or entry-point strings as text — the shader handle already
identifies the code, and a label must never change identity.

A cache is an object the caller creates and passes to a pipeline description. The
null backend validates the handle and reports no bytes, because it compiles
nothing; the D3D12 backend returns its PSO library blob in Phase 14 and the
Vulkan backend its `VkPipelineCache` data in Phase 34.

## Validation rules

Every rule is a `ErrorCode::InvalidArgument` unless noted, and has one static
message naming the rule.

**Shader**
- A valid stage and bytecode format.
- A non-empty code blob with a non-null pointer.
- A non-empty entry point for HLSL source, where the compiler needs one.

**Bind group layout**
- Between one and `Limits::maxBindingsPerGroup` entries.
- Binding numbers below `Limits::maxBindingsPerGroup` and unique.
- A known type, non-empty visibility, and `arrayCount >= 1`.

**Bind group**
- A live layout, and exactly as many resources as the layout's array counts sum
  to.
- Each resource's type matches its entry.
- A buffer range inside the buffer and aligned to
  `minUniformBufferOffsetAlignment` or `minStorageBufferOffsetAlignment`; a zero
  size means "the rest of the buffer".
- A buffer with the usage its binding type needs.
- A view of a texture with `Sampled` or `Storage` usage, and a live sampler.

**Pipeline layout**
- No duplicate group layout, at most four, and only in slot order.
- Push-constant ranges within the limit, non-overlapping per stage, and aligned.

**Graphics pipeline**
- A live layout and both shaders, in the right stages.
- A vertex layout with at least one binding and one attribute for a graphics
  pipeline; attributes point at live bindings.
- At most `Limits::maxColorAttachments` color attachments, each with a format
  that `FormatSupportsRenderTarget` accepts; blending only on a format that
  `FormatSupportsBlending` accepts; a write mask within RGBA.
- `depthStencilFormat` is either `Format::Count` (no depth attachment) or a depth
  format; a depth test needs one, and stencil needs a stencil format.
- One sample count for the pipeline and every attachment; MSAA needs one mip and
  a renderable format, which Phase 11 already enforces.
- Topology, cull mode, front face, and polygon mode are known enumerators.
- A compute shader in a graphics pipeline, or a vertex shader in a compute
  pipeline, is rejected as a stage mismatch.

**Compute pipeline**
- `Feature::Compute` (otherwise `ErrorCode::Unsupported`), a live compute shader,
  and a live layout.

## What this phase deliberately leaves out

- **Command lists, bind calls, and draws** are Phase 13. This phase makes the
  pipeline and group *objects*; using them is recording.
- **Reflection-driven layouts** are Phase 17, once the shader compiler can
  produce reflection. Layouts here are written by hand and validated by the
  backend.
- **Pipeline compilation on a job thread** is Phase 17. Creation is synchronous
  and the cache is a plain object.
- **Bindless descriptors** are Phase 19. `arrayCount` is a fixed-size descriptor
  array, not a runtime-indexed heap.
- **Stencil reference and blend constants** are per-command state and arrive with
  command lists.

## Example

```cpp
cyber::rhi::BindGroupLayoutEntry entries[2] = {};
entries[0].binding = 0;
entries[0].type = cyber::rhi::BindingType::UniformBuffer;
entries[0].visibility = cyber::rhi::ShaderStageBit(cyber::rhi::ShaderStage::Vertex);
entries[1].binding = 1;
entries[1].type = cyber::rhi::BindingType::SampledTexture;
entries[1].visibility = cyber::rhi::ShaderStageBit(cyber::rhi::ShaderStage::Pixel);

cyber::rhi::BindGroupLayoutDesc layoutDesc{};
layoutDesc.entries = cyber::memory::Span<const cyber::rhi::BindGroupLayoutEntry>(entries, 2);
auto layout = device.CreateBindGroupLayout(layoutDesc);

cyber::rhi::PipelineLayoutDesc pipelineLayoutDesc{};
pipelineLayoutDesc.groups[cyber::rhi::kBindGroupSlotPerMaterial] = layout.Value();
auto pipelineLayout = device.CreatePipelineLayout(pipelineLayoutDesc);

cyber::rhi::BindingResource resources[2] = {};
resources[0].type = cyber::rhi::BindingType::UniformBuffer;
resources[0].buffer = materialConstants;
resources[0].size = sizeof(MaterialConstants);
resources[1].type = cyber::rhi::BindingType::SampledTexture;
resources[1].textureView = albedoView;

cyber::rhi::BindGroupDesc groupDesc{};
groupDesc.layout = layout.Value();
groupDesc.resources = cyber::memory::Span<const cyber::rhi::BindingResource>(resources, 2);
auto group = device.CreateBindGroup(groupDesc);
```

## Verification

| Test binary | Proves |
|---|---|
| `rhi_shader` | Stage, format, code, and entry-point validation; handle lifetime and slot reuse. |
| `rhi_binding` | Layout entry rules, group resource matching, buffer ranges and alignment, usage requirements, view and sampler binding, stale handles. |
| `rhi_pipeline` | Every graphics and compute validation rule, including format and sample-count mismatches; pipeline lifetime and cache use. |
| `rhi_pipeline_hash` | Hashes are stable across calls, differ when a state differs, ignore labels, and change when a shader handle changes. |

Plus the four-preset build, the gate checks, and the package consumer creating a
shader, a layout, a group, and a pipeline through the exported target.

## Sources

- Microsoft, *Direct3D 12 programming guide*, resource binding, root
  signatures, and descriptor tables.
- Khronos, *Vulkan 1.3 specification*, chapters on pipelines, descriptor sets,
  descriptor set layouts, and push constants.
- W3C WebGPU candidate recommendation, `GPURenderPipelineDescriptor`,
  `GPUBindGroupLayout`, and `GPUBindGroup`.
- Microsoft, *HLSL programming guide*, entry points and semantics, for the
  location-to-semantic mapping.
- Akenine-Möller et al., *Real-Time Rendering*, 4th edition, chapter 3 on the
  graphics pipeline and chapter 5 on shading and binding.
