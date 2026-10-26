# Phase 12 report: pipelines and the binding model

Date: 2026-10-26
Machine: Windows 11, 13th Gen Intel Core i7, MSVC 19.44, CMake 3.31
Design note: [phase-12-pipelines-binding.md](../design/phase-12-pipelines-binding.md)
ADR: [0015](../adr/0015-pipeline-binding-model.md)
Contract: [`cyber_rhi`](../../engines/cyber_rhi/docs/MODULE.md)

## Done-when

> Every pipeline is described by plain structs and invalid combinations are
> rejected with clear messages, covered by tests.

Proved by four new test binaries with 27 cases between them, and by the fact
that every description type is trivially copyable and hashable:

- **Plain structs.** `GraphicsPipelineDesc`, `ComputePipelineDesc`,
  `BindGroupLayoutDesc`, `BindGroupDesc`, `PipelineLayoutDesc`, and
  `ShaderDesc` are aggregates of scalars, enums, spans, and handles. A static
  assertion in `rhi_public_headers` checks trivial copyability; the hash tests
  compare descriptions field by field through their effect on the hash.
- **Rejection with messages.** Every rule has one static message and one case in
  `rhi_pipeline` or `rhi_binding`, including stage mismatches, bad strides,
  attributes outside their buffer, duplicate locations, non-vertex formats,
  non-target formats, blend on integer targets, write masks above RGBA, sample
  count mismatches, depth state without an attachment, and push-constant
  overlap.
- **Coverage.** 67 of 67 tests pass in debug, release, shipping, and ASan.

## What was built

| Header | Contents |
|---|---|
| `shader.h` | `ShaderStage`, `ShaderStageMask` and `ShaderStageBit`, `ShaderBytecodeFormat`, `ShaderDesc`, `ShaderInfo`. |
| `binding.h` | `BindGroupSlot` (per frame, view, material, draw), `BindingType`, `BindGroupLayoutEntry/Desc`, `BindingResource`, `BindGroupDesc`, `BindGroupInfo`, `PushConstantRange`, `PipelineLayoutDesc`. |
| `pipeline.h` | Vertex bindings and attributes, topology, cull, front face, polygon mode, rasterizer state, blend factors and operations, color attachment state, multisample state, stencil operations and face state, depth-stencil state, `GraphicsPipelineDesc`, `ComputePipelineDesc`, `PipelineCacheDesc`, `PipelineKind`, `PipelineInfo`, and the two description hashes. |

`handle.h` gains `PipelineLayoutHandle` and `PipelineCacheHandle`.
`backend.h` gains seventeen entries; `device.h` gains the matching methods:
create and destroy for shaders, bind group layouts, bind groups, pipeline
layouts, graphics pipelines, compute pipelines, and pipeline caches, plus
`ShaderInfoOf`, `BindGroupInfoOf`, `PipelineInfoOf`, `PipelineCacheBytes`, and
`PipelineLayout`.

`src/pipeline.cpp` implements the hashes with `core::HashCombine`, hashing
handles through `PackHandle`, floats through their bit pattern, and spans
element by element. Labels and cache handles are excluded because neither
changes the compiled object.

The null backend copies layout entries, group resources, and push-constant
ranges into device-owned storage, so a caller's stack array can die after
creation. It validates a group against its layout at creation, and a pipeline
against every dependency at creation.

### What the interface implements today

Shader modules, layouts, groups, pipeline layouts, graphics and compute
pipelines, and caches are created, validated, queried, and destroyed. Pipeline
creation is synchronous and the hash is recorded. Nothing is bound or drawn:
that is Phase 13's recording model.

### Example

```cpp
cyber::rhi::GraphicsPipelineDesc desc{};
desc.layout = pipelineLayout;
desc.vertexShader = vertexShader;
desc.pixelShader = pixelShader;
desc.vertexLayout.bindings = bindings;
desc.vertexLayout.attributes = attributes;
desc.depthStencilFormat = cyber::rhi::Format::D32Float;
desc.colorAttachments = attachments;
desc.cache = pipelineCache;

auto pipeline = device.CreateGraphicsPipeline(desc);
```

## Decisions

- **Four binding slots, fixed by change frequency.** See ADR 0015. The slot order
  is the layout order, and a slot may be empty.
- **Flattened resources.** A layout with array counts `1, 3, 1` takes five
  resources in binding order, so both arrays walk together.
- **Textures and samplers bind separately.** No combined image sampler, which
  one API has and another does not.
- **Locations, not semantics.** `location N` maps to `TEXCOORDN` in D3D12, which
  the Phase 16 HLSL generator will also emit.
- **One hash per description.** Labels do not affect identity; a reloaded shader
  does, because its handle's generation changes.
- **The cache is an object, not a deduplicating table.** Two creations produce
  two independent handles, so a destroy never invalidates another owner.
- **Push constants are up to four ranges.** D3D12 root constants and Vulkan push
  constants fit one shape.
- **`R32G32B32Float` was added to the format table.** A three-component vertex
  position is the one layout the Phase 11 table lacked; without it every vertex
  layout would need a fourth float. The table test now walks 29 formats.

## Evidence

| Check | Result |
|---|---|
| `scripts/build.ps1 -Preset debug` | pass |
| `scripts/build.ps1 -Preset release` | pass |
| `scripts/build.ps1 -Preset shipping` | pass |
| `scripts/build.ps1 -Preset debug-asan` | pass |
| `ctest` debug / release / shipping / debug-asan | **67/67 each** — 63 existing plus 4 new binaries |
| `scripts/check_structure.py` | 16 modules, 1 contract |
| `scripts/layer_check.py` | 16 modules |
| `scripts/check_format.py` | 232 files |
| `scripts/verify_package.ps1` | installed consumer configures, links, runs — exit 0 |
| source consumer | configures, links, runs — exit 0 |

The ASan runtime DLL still needs `VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64` on
`PATH`; with it, 67/67 pass.

New test binaries:

| Test | Cases | What it proves |
|---|---|---|
| `rhi_shader` | 3 | Stage, format, code, and entry-point rules; binary formats need no entry point; lifetime and slot reuse. |
| `rhi_binding` | 6 | Layout entry rules and collisions; group resource counts, types, buffer ranges, alignment, usage, views, samplers, stale handles; groups outlive their layout; array-count flattening; pipeline layouts, slot duplication, and every push-constant rule. |
| `rhi_pipeline` | 6 | Graphics and compute creation; shader stage mismatches; stale layout, shader, and cache; every vertex-layout rule; attachment, blend, and depth rules; multisample rules; cache round trip, seeding, and lifetime. |
| `rhi_pipeline_hash` | 2 | Hashes are stable, labels are excluded, every state field changes the hash, shader generation changes it, and the function is pure. |

Allocator accounting is asserted after shader, binding, and pipeline creation:
`HeapAllocator::Stats().used` returns to zero, so the copied arrays are released
with their objects and at device teardown.

The installed and source consumers now create a vertex and a pixel shader, a
group layout, a pipeline layout, a uniform buffer, a bind group, and a graphics
pipeline, verify the recorded hash equals the description hash, and destroy
everything through the exported `cyber::rhi` target.

## Known gaps

- **No bind or draw.** Command lists and the recording API are Phase 13.
- **No reflection.** Layouts are written by hand; Phase 17 derives them from the
  compiler.
- **No asynchronous compile.** Pipeline creation is synchronous and may stall.
  Phase 17 adds compilation on the job system with a fallback pipeline.
- **The null backend compiles nothing.** Its cache saves an empty blob; the
  cache interface and its validation are what this phase proves. D3D12's PSO
  library and Vulkan's `VkPipelineCache` data arrive with those backends.
- **`Unsupported` for compute is unreachable locally.** The null backend reports
  `Feature::Compute`, as Phase 10 established, so the compute gate is exercised
  by the D3D12 conformance tests.
- **Stencil reference and blend constants** are per-command state and arrive
  with command lists.
- **Attribute locations are not checked against shader inputs.** Without
  reflection, a mismatch surfaces when the pipeline is used in Phase 13 and at
  compile time once Phase 16 emits both sides.
- **Static error messages**, unchanged from Phase 11: formatted diagnostics wait
  for Phase 15's error sink.

## Sources

- Microsoft, *Direct3D 12 programming guide*: resource binding, root signatures,
  descriptor tables, and root constants.
- Khronos, *Vulkan 1.3 specification*: pipelines, descriptor set layouts,
  descriptor sets, and push constants.
- W3C WebGPU candidate recommendation: `GPURenderPipelineDescriptor`,
  `GPUBindGroupLayout`, and `GPUBindGroup`.
- Microsoft, *Programming guide for HLSL*: entry points and semantics.
- Akenine-Möller et al., *Real-Time Rendering*, 4th edition: the graphics
  pipeline and shading stages.

## What the next phase needs from this one

- `DeviceInterface` is still the only backend surface; Phase 13 adds command
  list and submission entries to the same table.
- `PipelineHandle`, `BindGroupHandle`, and `PipelineLayoutHandle` are what a
  command list binds; the four-slot order is the bind order.
- `PushConstantRange` is what the command list's push-constant write validates
  against.
- The resource state vocabulary from Phase 11 plus this phase's attachment
  formats are what a render pass checks when it begins.
- The validation style — one message per rule, one test per message — carries
  into command recording.
