# Phase 14 — Direct3D 12 backend

Date: 2026-10-26
Status: approved (increments land one at a time)
Related: ADR 0017 (D3D12 backend), ADR 0013 (RHI object model), ADR 0016
(command recording), [`cyber_rhi` contract](../../engines/cyber_rhi/docs/MODULE.md),
[Phase 13 design note](phase-13-commands-sync.md)

## What this phase builds

The first backend that drives hardware. Phases 10 to 13 defined an interface and
proved it against a backend that validates but renders nothing; Phase 14 has to
prove the same interface can carry real work, on a real API, without the
interface changing shape to suit that API.

The done-when line is two images: a colored triangle and a textured rotating
cube, rendered through the RHI on D3D12, matching what `cyber_software_renderer`
produces within a recorded tolerance. That comparison is the point of the phase:
the software renderer is the reference implementation of the engine's
conventions, so a GPU image that disagrees is either a convention bug or a
backend bug, and the difference image says which.

## What it deliberately leaves out

- **No mesh shaders, sparse resources, or bindless arrays.** They are optional
  features on the RHI's capability list and Phase 15 onward can add them without
  changing the object model.
- **No GPU memory allocator.** Every resource here gets its own heap; Phase 15
  replaces that with sub-allocation and aliasing.
- **No validation layer.** The debug layer's messages are surfaced and treated as
  failures, but the RHI-level checks that survive a release build are Phase 15.
- **No async pipeline compilation.** Pipeline creation blocks, as it does in the
  null backend; Phase 17 moves it to the job system.
- **No shader reflection.** Layouts stay hand-written until Phase 17 derives them
  from the compiler.

## Interfaces it touches

`cyber/rhi/d3d12.h` is the only new public header: `IsD3D12CompiledIn()` and
`D3D12Backend()`. Everything else this phase needs already exists — the `Backend`
table, `DeviceInterface`, the recording API, and the resource descriptions.
Nothing in `include/cyber/rhi/` except this one header changes, which is the
strongest evidence that the Phase 10 boundary was drawn in the right place.

The backend's sources live in `engines/cyber_rhi/src/d3d12/`, the only directory
in the renderer that includes `d3d12.h` or `dxgi*.h`.

## Design

### Increments

The phase lands in six increments, each one committed with its own tests:

| Increment | Contents | Evidence |
|---|---|---|
| 1. Adapters | DXGI factory, hardware adapters, WARP last, `Backend` entry point | `rhi_d3d12_adapters`, 6 cases |
| 2. Device | `D3D12CreateDevice`, three queues, timeline fences, `Capabilities` | device conformance against the null backend's suite |
| 3. Descriptors | CPU staging heaps, a shader-visible ring per frame in flight | descriptor allocation tests |
| 4. Upload | Upload ring for dynamic data, staging for textures | a buffer written and read back on the GPU |
| 5. Shaders | `D3DCompile` from the Windows SDK, bytecode cached by description hash | compile-failure and reflection-free layout tests |
| 6. Pipelines and images | Root signatures from the bind-group layouts, PSOs, the triangle and the cube | the two reference images |

The roadmap checkbox stays unticked until increment 6 produces both images.

### Adapter discovery

`enumerateAdapters` is a pure function of the machine: create a factory, walk
`EnumAdapters1` up to a bounded cap, skip software entries, then append
`EnumWarpAdapter` last, release everything. No cached factory, no singleton, no
teardown. The contract is "write what fits, return the total", which the test
exercises with a zero-capacity query and a one-entry span.

### The mapping

Every RHI concept has exactly one D3D12 counterpart. This table is the contract
the backend is written against, and a row with no counterpart is a design bug to
resolve here rather than in the backend.

| RHI | D3D12 | Notes |
|---|---|---|
| `Backend::enumerateAdapters` | `IDXGIFactory4::EnumAdapters1` + `EnumWarpAdapter` | WARP appended last. |
| `AdapterInfo::dedicatedVideoMemoryBytes` | `DXGI_ADAPTER_DESC1::DedicatedVideoMemory` | 0 for WARP. |
| `Device` | `ID3D12Device` | Feature level from `D3D12CreateDevice`, reported through `Capabilities`. |
| `QueueKind::Graphics` | `D3D12_COMMAND_LIST_TYPE_DIRECT` + a queue | One queue per class, aliased when the adapter has fewer. |
| `QueueKind::Compute` | `D3D12_COMMAND_LIST_TYPE_COMPUTE` | |
| `QueueKind::Copy` | `D3D12_COMMAND_LIST_TYPE_COPY` | |
| `FenceHandle` | `ID3D12Fence` | Timeline semantics are native: `Signal`/`SetEventOnCompletion`. |
| `BufferHandle` | `ID3D12Resource` (`D3D12_RESOURCE_DIMENSION_BUFFER`) | `HostVisible` = `UPLOAD`, `Readback` = `READBACK`, else `DEFAULT`. |
| `TextureHandle` | `ID3D12Resource` | Row pitch aligned to 256 bytes for uploads. |
| `ViewHandle` | A descriptor in a shader-visible heap | CPU staging heap mirrored to the ring at bind time. |
| `SamplerHandle` | A sampler descriptor | Static samplers where the root signature allows. |
| `BindGroupLayoutDesc` | Root signature parameter list | Four slots = four descriptor tables plus root constants. |
| `PushConstantRange` | `D3D12_ROOT_CONSTANTS` | Four ranges maximum, matching Vulkan. |
| `GraphicsPipelineDesc` | `D3D12_GRAPHICS_PIPELINE_STATE_DESC` | Blend, depth, rasterizer, and input layout map field for field. |
| `CommandList` | `ID3D12GraphicsCommandList` + allocator | One allocator per frame in flight, reset by the frame ring. |
| `RenderPassDesc` | `OMSetRenderTargets` + `ClearRenderTargetView`/`ClearDepthStencilView` | Load and store ops are explicit barriers plus clears. |
| `ResourceState` | `D3D12_RESOURCE_STATES` | One to one, including `PRESENT` and `RENDER_TARGET`. |
| `Barrier` | `D3D12_RESOURCE_BARRIER` | Transition, aliasing, and UAV barriers; enhanced barriers are Phase 15. |
| `SwapChain` | `IDXGISwapChain3` | Flip model, `CreateSwapChainForHwnd`; images are `RENDER_TARGET` then `PRESENT`. |
| `PresentMode::Fifo` | `DXGI_SWAP_EFFECT_FLIP_DISCARD` with vsync | `Mailbox` maps to `DXGI_PRESENT_ALLOW_TEARING`. |
| `SubmitDesc` | `ExecuteCommandLists` + `Signal` | Wait values become `SetEventOnCompletion` on the CPU before the call. |
| `RetirementQueue` | No counterpart | The fence value the RHI already tracks is what the backend waits on. |

Two rows deserve a note. **Render-pass load and store ops** have no D3D12 call:
`LoadOp::Clear` becomes a clear after the transition, `LoadOp::Load` becomes a
transition that preserves contents, and `StoreOp::DontCare` becomes a
`DISCARD`-hinted transition. The RHI keeps the abstract operation because Vulkan
and WebGPU do have it, and the mapping is the backend's job. **Vertex attribute
locations** become `SV_Position`-relative `POSITION`, `TEXCOORDn`, and
`COLORn` semantics through the Phase 16 HLSL generator; locations are what the
RHI describes and semantics are what D3D12 needs.

### Conventions at the edge

The RHI's conventions are the engine's, not the API's: +Y up in clip space 0..1
reverse-Z, top-left texture origin, right-handed view, column-major matrices.
D3D12 disagrees on two of them — its depth range is 0..1 but it expects a
left-handed view by default and its texture origin is top-left — so the backend
uses the predicates in `conventions.h` rather than its own answers. The
projection matrix carries the reverse-Z mapping; nothing in the backend negates a
matrix.

### Debug layer

The debug layer and GPU-based validation are enabled from the first line of
device code, and the message queue is drained after every submission. A message
at `CORRUPTION` or `ERROR` severity fails the submission; `WARNING` is recorded
in the frame report. This is stricter than the API default and is what the build
guide asks for.

## Alternatives considered

- **Vulkan first.** Rejected: the target platform is Windows, D3D12's explicit
  model is the closest to what Phase 10 to 13 already describe, and the debug
  layer is the best available tool for a first backend.
- **A minimal backend that renders the triangle and nothing else.** Rejected: it
  would prove the interface can carry a draw, not that it can carry a frame, and
  the cube's image comparison is what catches a texture-origin or a reverse-Z
  mistake.
- **Throwing the interface away and re-deriving it from D3D12.** Rejected: that
  is what ADR 0013 exists to prevent, and the mapping table is the evidence that
  no entry needs to change.
- **Rendering to an offscreen texture instead of a swapchain.** Rejected as the
  only path: the image comparison needs a deterministic target, so the comparison
  runs offscreen while the swapchain is exercised by the sample.

## Risks

- **Feature-level divergence.** A 12_0 adapter lacks `SV_Barycentrics` and some
  formats; the backend queries `CheckFeatureSupport` rather than assuming 12_1.
- **Descriptor-heap pressure.** A ring per frame in flight is the simplest correct
  design, but a frame that binds more descriptors than the ring holds stalls. The
  ring's size is a limit in `Capabilities`, and exhaustion is a reported error.
- **The upload ring's coherency.** `UPLOAD` heaps are write-combined, so a read
  back through the same memory is slow and wrong; the tests read back through a
  `READBACK` resource instead.
- **WARP in CI is slow.** The image comparison on WARP runs at a small resolution
  with a fixed seed, and the tolerance is recorded with the reference.

## Test plan

| Test | Proves |
|---|---|
| `rhi_d3d12_adapters` | Adapter enumeration, WARP last, the short-span contract, and that an incomplete table is a reported error. |
| device conformance | The same cases the null backend passes, run on D3D12 and on WARP. |
| descriptor ring | Allocation, exhaustion, and reset per frame. |
| upload and readback | Bytes written on the CPU arrive on the GPU and come back unchanged. |
| pipeline build | Every `GraphicsPipelineDesc` field reaches `D3D12_GRAPHICS_PIPELINE_STATE_DESC`, and an invalid combination still fails at creation. |
| image comparison | The triangle and the cube match the software renderer within the recorded tolerance, on hardware and on WARP. |

## Sources

- Microsoft, *Direct3D 12 programming guide*: adapters, devices, queues, fences,
  descriptors, heaps, and the swapchain.
- Microsoft, *Understanding the D3D12 debug layer*: GPU-based validation and
  DRED.
- Microsoft, *Programming guide for HLSL*: semantics, the compiler targets.
- Microsoft, *DXGI 1.6* and *flip-model swap chains*.
- Khronos, *Vulkan 1.3 specification*: read for the places where the two APIs
  disagree, so the interface keeps the union of what both can express.
