# cyber_rhi — module contract

**Kind:** graphics
**Layer:** 2
**Order:** 20
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

GPU hardware interface: device, queues, resources, pipelines, command lists, synchronization, swapchain, and backends.

## Public API

`include/cyber/rhi/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

| Header | Contents |
|---|---|
| `handle.h` | `Handle<Tag>` aliases for every object type, validity, `PackHandle`/`UnpackHandle`. |
| `format.h` | `Format`, the `FormatInfo` numeric table, `FormatInfoOf`, classification, sRGB/linear variants, view compatibility, and support rules. |
| `conventions.h` | Boundary constants (+Y up, reverse-Z 0..1, top-left origin) and the per-backend mapping predicates. |
| `capabilities.h` | `BackendKind`, `AdapterInfo`, `Limits`, `Feature`, `Features`, `Capabilities`. |
| `descriptors.h` | `QueueKind`, `ValidationLevel`, `DeviceDesc`, `FenceDesc`, `SurfaceDesc`. |
| `buffer.h` | `BufferUsage`, `BufferDesc`, `BufferInfo`, and usage set helpers. |
| `texture.h` | `TextureDimension`, `TextureUsage`, `TextureAspect`, `SubresourceRange`, `TextureDesc`, `TextureViewDesc`, `TextureInfo`, `TextureViewInfo`, `MaxMipLevels`, `TextureByteSize`. |
| `sampler.h` | `FilterMode`, `AddressMode`, `BorderColor`, `CompareFunction`, `SamplerDesc`. |
| `shader.h` | `ShaderStage`, `ShaderStageMask`, `ShaderBytecodeFormat`, `ShaderDesc`, `ShaderInfo`. |
| `binding.h` | `BindGroupSlot`, `BindingType`, group layout and group descriptors, `PipelineLayoutDesc`, `PushConstantRange`. |
| `pipeline.h` | Vertex layout, topology, rasterizer, blend, multisample, depth-stencil state, `GraphicsPipelineDesc`, `ComputePipelineDesc`, `PipelineCacheDesc`, and the stable description hashes. |
| `resource_state.h` | `ResourceState` and its set rules, `BufferBarrier`, `TextureBarrier`, `ResolveSubresourceRange`. |
| `render_pass.h` | `LoadOp`, `StoreOp`, clear values, color and depth attachments, `RenderPassDesc`, `RenderPassInfo`, and the structural validator. |
| `command_list.h` | Draw, dispatch, and copy arguments, index formats, viewport and scissor, `CommandListDesc`, the recording state machine, `CommandListStats`, `CommandListInterface`, and `CommandList`. |
| `submission.h` | `FenceSignal`, `GpuWait`, `SubmitDesc`, `SubmissionInfo`, and the submission validator. |
| `swapchain.h` | `PresentMode`, `SwapChainDesc`, `SwapChainFrame`, `SwapChainInfo`, `SwapChainInterface`, and `SwapChain`. |
| `frame_ring.h` | `FrameRingDesc`, `FrameSlot`, and `FrameRing`: frames in flight over one timeline. |
| `retirement.h` | `RetireKind`, `RetireEntry`, and `RetirementQueue`: destruction keyed by fence value. |
| `d3d12.h` | `IsD3D12CompiledIn()` and `D3D12Backend()`: the Direct3D 12 entry point, built only when `CYBER_RHI_ENABLE_D3D12` is on. |
| `backend.h` | `Backend`, `DeviceInterface` (device, fence, resource, barrier, command list, submission, and swapchain entries), `DeviceCreation`, `EnumerateAdapters`, `NullBackend`. |
| `device.h` | `Device`: queues, timeline fences, buffers, textures, views, samplers, host access, state queries, transitions, command lists, submission, swapchains, and `WaitIdle`. |
| `rhi.h` | Umbrella include. |

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_platform, cyber_jobs, cyber_perf, cyber_stability

Forbidden: cyber_renderer, cyber_graphics, cyber_ui

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Only this module includes D3D12, Vulkan, or Metal headers.
- Synchronization is explicit; there are no hidden barriers.
- Resources use generation-counted handles and deferred destruction.
- The null backend validates every public operation.

## Non-goals

- Render graph, materials, lighting, and passes (cyber_renderer).
- Window creation (cyber_platform).

## Tests

Null-backend creation, lifetime and invalid-use tests; barrier validation; backend conformance suite.

Phase 10 delivers `rhi_public_headers`, `rhi_handle`, `rhi_format`,
`rhi_conventions`, `rhi_capabilities`, `rhi_device`, and `rhi_fence`.

Phase 11 adds `rhi_format_table`, `rhi_buffer`, `rhi_texture`, `rhi_sampler`,
and `rhi_resource_state`. Every rule a real backend must enforce — descriptor
shape, limits, capability, format compatibility, host visibility, state
matching, and stale handles — has a case that fails if the rule is removed.

Phase 12 adds `rhi_shader`, `rhi_binding`, `rhi_pipeline`, and
`rhi_pipeline_hash`: stage and bytecode validation, layout and group matching,
push-constant ranges, every graphics and compute state rule, and stable hashes.

Phase 13 adds `rhi_render_pass`, `rhi_command_list`, `rhi_submit`,
`rhi_missing_barrier`, `rhi_frame_ring`, `rhi_retirement`, and `rhi_swapchain`:
the recording state machine, every command's rules, batch submission, present and
resize, retirements that wait for a fence, and the two tests the done-when line
names.

Phase 14 adds `rhi_d3d12_adapters` — DXGI adapter enumeration with Microsoft's
WARP software adapter last, the short-span contract, and a complete per-device
table — and `rhi_d3d12_device`: a real `ID3D12Device` on a chosen adapter, three
command queues, timeline fences over `ID3D12Fence` with GPU signals and
event-based waits, `Capabilities` from the feature level and the API's
guaranteed limits, and a drain on `WaitIdle`.

## Budgets

CPU submission cost measured. No per-draw heap allocation.

## Roadmap

Phases 10, 11, 12, 13, 14, 15, 34, 35.

Phase 11 is complete: resources, views, samplers, the format table, and the
resource state model, validated by the null backend.

Phase 12 is complete: shader modules, the four-slot binding model, pipeline
layouts with push constants, graphics and compute pipelines, and a hash-keyed
pipeline cache, all validated by the null backend.

Phase 13 is complete: command lists, render passes, submission with timeline
fences, the frame ring, the retirement queue, and the swapchain. The null backend
validates every recorded command against a per-list shadow of resource state, so
a missing barrier is a record-time error naming the resource.

Phase 14 is in progress: adapter discovery and the device are in place, and the
descriptor heaps, the upload ring, shader compilation, and the two reference
images land in order behind them. Every entry this increment has not reached yet
answers `Unsupported` with a message naming the increment, so a caller is never
lied to about what a device can do; `Capabilities::features` advertises only
`TimelineFences` for the same reason. `cyber/rhi/d3d12.h` is the only public header the
phase adds. The backend lives under `src/d3d12/`, the only directory in the
renderer that includes a graphics API header.

## Notes

- State is tracked per `(mip, array layer)`, not per aspect: D3D12 cannot express
  a per-aspect barrier, and Vulkan's second layout is derived from one state in
  Phase 34.
- Destruction is immediate: `Destroy*` releases storage when it is called. The
  retirement queue in `retirement.h` is the caller's policy for holding an object
  until its fence passes, and it lives above the backend on purpose.
- The null backend commits a list's transitions when the list ends rather than
  when it is submitted, because it has no execution timeline. A real backend
  reaches them when the list runs; Phase 14 records the difference in its
  conformance notes.
- Error messages are static strings until Phase 15 provides an error sink that
  can format a value without allocating.
