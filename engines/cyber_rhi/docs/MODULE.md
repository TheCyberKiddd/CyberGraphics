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
| `command_list.h` | Draw, dispatch, copy, timestamp, debug-label, and point-marker commands, index formats, viewport and scissor, `CommandListDesc`, the recording state machine, `CommandListStats`, `CommandListInterface`, and `CommandList`. |
| `submission.h` | `FenceSignal`, `GpuWait`, `SubmitDesc`, `SubmissionInfo`, and the submission validator. |
| `timestamp.h` | `TimestampQueryHeapDesc` for per-frame GPU timestamp slots. |
| `swapchain.h` | `PresentMode`, `SwapChainDesc`, `SwapChainFrame`, `SwapChainInfo`, `SwapChainInterface`, and `SwapChain`. |
| `frame_ring.h` | `FrameRingDesc`, `FrameSlot`, and `FrameRing`: frames in flight over one timeline. |
| `retirement.h` | `RetireKind`, `RetireEntry`, and `RetirementQueue`: destruction keyed by fence value. |
| `d3d12.h` | `IsD3D12CompiledIn()` and `D3D12Backend()`: the Direct3D 12 entry point, built only when `CYBER_RHI_ENABLE_D3D12` is on. |
| `backend.h` | `Backend`, `DeviceInterface` (device, fence, resource, barrier, command list, submission, query heap, timestamp readback, leak report, and swapchain entries, plus the debug-only `objectName` read-back), `ObjectKind`, `ObjectRef`, `ObjectNameInfo`, `DeviceCreation`, `EnumerateAdapters`, `NullBackend`. |
| `device.h` | `Device`: queues, timeline fences, buffers, textures, views, samplers, timestamp query heaps and readback, memory statistics, leak reports, host access, state queries, transitions, command lists, submission, swapchains, `WaitIdle`, and the debug-only `ObjectName` read-back. |
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
table — `rhi_d3d12_device`: a real `ID3D12Device` on a chosen adapter, three
command queues, timeline fences over `ID3D12Fence` with GPU signals and
event-based waits, `Capabilities` from the feature level and the API's
guaranteed limits, and a drain on `WaitIdle` — `rhi_d3d12_resources`: buffers on
the heap their usage names, textures, views, samplers, host-visible writes and
readback reads, and the barrier path with per-subresource texture state — and
`rhi_d3d12_binding`: HLSL compiled through the SDK's `D3DCompile`, bind group
layouts and groups filling one contiguous block of the shader-visible heap, and
pipeline layouts serialized into root signatures.

Phase 15 adds GPU heap allocation and transient reuse, a shared RHI validation
wrapper, driver-backed object names, native D3D12 pass markers, timestamp
readback, and named leak reporting. `rhi_device_lost` forces removal on WARP
through `ID3D12Device5::RemoveDevice`, verifies sticky `ErrorCode::DeviceLost`
results and logged DRED/queue diagnostics, and confirms that destruction remains
available for cleanup after removal.

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

Phase 14 is complete: adapter discovery, the device, queues and timeline fences,
resources, HLSL shaders and the binding model, pipeline state with pipeline
libraries, command recording and submission over a real
`ID3D12GraphicsCommandList`, a flip-model swapchain, and the image comparison
that renders a colored triangle and a textured cube through the interface and
matches `cyber_software_renderer` within a recorded tolerance (`backend_compare`,
under `tests/integration/`, because it is the one test that may link both
renderers). Resources
carry their own D3D12 state, and a transition is submitted through a one-shot
list and waited on, because the API has no immediate-mode barrier. Every entry this backend has not reached yet
answers `Unsupported` with a message naming it, so a caller is never
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
- The D3D12 backend has two format conversions, not one: `ToDxgiFormat` answers
  for texels and returns `DXGI_FORMAT_UNKNOWN` for `R32G32B32Float`, while
  `ToVertexFormat` answers for vertex fetch, where the API does have
  `DXGI_FORMAT_R32G32B32_FLOAT`. A vertex attribute is not a texel.
- A Direct3D 12 pipeline cache is an `ID3D12PipelineLibrary`, keyed by the hash of
  the pipeline description. It is an optimization: a cache that cannot answer is
  a miss, and creating one never fails because of its seed bytes.
- A bind group writes its own view and sampler descriptors into its block of the
  shader-visible heap and copies none: Direct3D 12 makes a shader-visible heap
  write-only for the CPU, so a descriptor read back out of one is invalid. The
  API's `CopyDescriptors` path is therefore unused by this backend.
- A barrier whose entry is already in the state the resource's tracking holds
  records nothing. Direct3D 12 rejects a transition whose two states are equal,
  which fails the whole list at `Close`, so a repeated transition is a no-op here
  rather than an error that names neither the resource nor the caller.
- The debug layer, with GPU-based validation, is enabled before the device exists
  in debug builds. It has no reader of its own, so the backend drains its queue
  when the device is destroyed and reports what it found on `LogChannel::Rhi` —
  errors at error level, warnings at warning level — which is what lets a test
  fail on any message. Two by-design messages (the startup banner and the
  missing-optimized-clear note) are filtered at the queue.
- A submitted command list must not be destroyed, or have its allocator reset,
  while the queue is still executing it; `Device::CreateCommandList` documents the
  fence a caller has to wait on first.
