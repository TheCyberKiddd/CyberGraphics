# Phase 10 notes — the D3D12, Vulkan, and WebGPU object models

Date: 2026-10-25

This note records what the three APIs actually require of a caller, so the RHI
can be the intersection plus a declared set of optional features rather than a
copy of whichever API was read first. Everything below is from the public
documentation listed at the end; no driver or engine source was read.

## 1. Device and queues

| Concept | D3D12 | Vulkan | WebGPU |
|---|---|---|---|
| Adapter enumeration | `IDXGIFactory4::EnumAdapters1`, then `D3D12CreateDevice` | `vkEnumeratePhysicalDevices`, no factory object | `navigator.gpu.requestAdapter()` |
| Device | `ID3D12Device`, multiple nodes for linked adapters | `vkCreateDevice` with one queue per requested family, queue create flags and priorities | one `GPUDevice` per adapter; no queue configuration at all |
| Queue classes | `DIRECT`, `COMPUTE`, `COPY`, plus video classes | families expose flags: graphics, compute, transfer, sparse, video | one implicit `GPUQueue` that does everything |
| Queue count | caller creates as many as needed | fixed set chosen at device creation, one per family index | one |
| Queue priorities | `D3D12_COMMAND_QUEUE_PRIORITY`, plus per-node masks | `VkDeviceQueueCreateInfo.pPriorities` | none |
| Capability query | `CheckFeatureSupport` with a `D3D12_FEATURE_*` selector; `ID3D12Device1..9` indicate growth | `vkGetPhysicalDeviceFeatures2`, `...Properties2`, extension enumeration | adapter `limits` and `features` objects, fixed by the spec |

The important asymmetry: Vulkan makes the queue set a *construction* decision and
WebGPU has exactly one, while D3D12 lets you create more at will. An RHI that
hands out one queue per class and never lets a caller create one keeps all three
possible. A backend that only has one physical queue can alias all three classes
onto it, which is what a null backend must do and what a validation layer must
warn about when it sees a copy on the graphics queue.

## 2. Command recording

| Concept | D3D12 | Vulkan | WebGPU |
|---|---|---|---|
| Encoder | `ID3D12GraphicsCommandList` written into an `ID3D12CommandAllocator` | `VkCommandBuffer` from a `VkCommandPool` | `GPUCommandEncoder` producing a `GPUCommandBuffer` |
| Reset model | allocator reset only when no list from it is in flight; lists have explicit `Reset` | pool reset invalidates all buffers from it, or `vkResetCommandBuffer` with a flag | create a new encoder; finished command buffers are immutable |
| Threading | a list and its allocator are used by one thread at a time | external synchronization on pool and buffer | one encoder per thread; no sharing |
| Bundles | bundle lists (rarely used) | secondary command buffers with inheritance | render bundles |
| Render pass | `OMSetRenderTargets` + optional `BeginRenderPass` for tiled renderers | `vkCmdBeginRenderPass` with `VkRenderPass`/`VkFramebuffer`, or dynamic rendering | implicit: `beginRenderPass` with a color/depth attachment descriptor |
| Compute | `Dispatch` | `vkCmdDispatch` | `beginComputePass` |

All three agree that a command list is recorded by one thread at a time and
becomes read-only at submit. Vulkan and WebGPU both make the *pool* the unit of
reset; D3D12 makes the allocator the unit. Modelling a per-frame command list
that the RHI resets before recording is portable to all three.

## 3. Memory

| Concept | D3D12 | Vulkan | WebGPU |
|---|---|---|---|
| Allocation | `ID3D12Heap` + committed/placed/reserved resources | `VkDeviceMemory` with `memoryTypeBits` from the resource's requirements | none visible; `GPUBuffer` is either mappable or not |
| Heap classes | default (device-local), upload (host-visible, write-combined), readback (host-visible, cached) | memory types with flags: device-local, host-visible, host-coherent, host-cached | implicit |
| Sub-allocation | caller's job (`ID3D12Heap` placement), engine or D3D12MA | caller's job (VMA or equivalent) | not possible |
| Residency | explicit `MakeResident`/`Evict` on some budgets, paging handled by DXGI otherwise | implicit for `VK_EXT_memory_budget`-style reporting; sparse bindings are explicit | implicit |
| Aliasing | placed resources on the same heap with a predictor | `vkBindImageMemory`/`vkBindBufferMemory` with aliasing flags | not exposed |
| Mapping | `Map`/`Unmap` on upload and readback resources | `vkMapMemory` on coherent types, flush/invalidate otherwise | `mapAsync` + `getMappedRange` |

The RHI therefore needs a memory *class* per resource (device-local, upload,
readback) and nothing else at the boundary in Phase 11. Sub-allocation, aliasing,
and residency are Phase 15 work and belong behind the resource, not in its
descriptor.

## 4. Resources and binding

| Concept | D3D12 | Vulkan | WebGPU |
|---|---|---|---|
| Buffer | `ID3D12Resource` | `VkBuffer` | `GPUBuffer` with `usage` bits |
| Texture | `ID3D12Resource` with a `D3D12_RESOURCE_DESC` | `VkImage` plus a `VkImageView` for each use | `GPUTexture` plus `GPUTextureView` |
| Sampler | `D3D12_SAMPLER_DESC` in a sampler heap | `VkSampler` | `GPUSampler` |
| View creation | descriptor in a heap (CBV/SRV/UAV or DSV), one descriptor per view | explicit `VkImageView` object | explicit `GPUTextureView` object |
| Binding mechanism | root signature: root constants, root descriptors, descriptor tables into heaps | descriptor set layout + `VkPipelineLayout`, descriptor pools, push constants | bind group layout + bind group, pipeline layout, 4 slots by convention |
| Binding update | write descriptors any time the memory is not in flight | `vkUpdateDescriptorSets` with a pool flag (`UPDATE_AFTER_BIND` optional) | immutable after creation (or `copy` bind groups) |
| Shader bytecode | DXIL in a blob | SPIR-V in a module | WGSL text |
| Pipeline state | monolithic `ID3D12PipelineState`, root signature is a separate object | monolithic `VkPipeline`, `VkPipelineCache` | `GPURenderPipeline`/`GPUComputePipeline`, implicit cache |

Every one of these separates *what a shader sees* (a group of resources at a
slot) from *how it is bound* (a root table, a descriptor set, or a bind group).
The RHI's binding model is the former: groups of typed slots, with the layout
created up front and immutable after creation. That is the only shape that maps
onto all three without a per-backend special case.

## 5. Synchronization

| Concept | D3D12 | Vulkan | WebGPU |
|---|---|---|---|
| Resource transitions | `ResourceBarrier` with `D3D12_RESOURCE_STATES` | `vkCmdPipelineBarrier2` with image layouts, stage masks, access masks | none; a pass's resource usage is the barrier |
| Aliasing barrier | explicit | explicit (aliasing) | not exposed |
| UAV/write hazard | `UAVBarrier` | barrier with shader-write access | implicit |
| GPU fence | `ID3D12Fence` with a monotonically increasing value | `VkSemaphore` (binary or timeline) and `VkFence` (binary) | `onSubmittedWorkDone`, no values |
| CPU wait | `SetEventOnCompletion` + `WaitForSingleObject` | `vkWaitSemaphores` / `vkWaitForFences` | awaited promise |
| Queue ordering | within a queue only | within a queue only; semaphores across queues | implicit total order on one queue |
| Frame pacing | `IDXGISwapChain3::SetMaximumFrameLatency`, waitable object | present modes + `VK_KHR_present_wait` | browser-scheduled |

Superseding note: D3D12 fences carry values natively, Vulkan needs
`VK_KHR_timeline_semaphore` (core in 1.2) to match, and WebGPU has no values at
all. A timeline fence is therefore the RHI's model, and a backend without
timeline semantics (WebGPU, or a Vulkan device without the feature) emulates it
with a small value-to-binary-semaphore map. That is a backend detail the design
note records so it is not discovered in Phase 34.

Explicit transitions are the harder decision. WebGPU has none, so a transition
that the RHI requires is a no-op there; D3D12 and Vulkan require one. The RHI
therefore keeps an explicit barrier command and lets a backend treat it as a
no-op, instead of inferring barriers and hiding them: a hidden barrier is
unpredictable across APIs and impossible to time.

## 6. Swapchain and presentation

| Concept | D3D12 | Vulkan | WebGPU |
|---|---|---|---|
| Surface | HWND or composition surface at swapchain creation | `VkSurfaceKHR` from the platform, format and present mode negotiated | canvas context configured from the canvas |
| Buffers | DXGI owns them; `GetBuffer` returns a resource the app must transition | `vkGetSwapchainImagesKHR`, app creates views | `getCurrentTexture()` per frame |
| Acquire | implicit: the swapchain index after `Present` | `vkAcquireNextImageKHR` with a semaphore and fence | implicit per frame |
| Present modes | `FLIP_DISCARD`, `FLIP_SEQUENTIAL`; tearing needs a flag | FIFO, MAILBOX, IMMEDIATE; `VK_KHR_shared_presentable_image` | `preferred`, plus fifo/mailbox on some platforms |
| Resize | `ResizeBuffers` with all references released | recreate the swapchain, old one retired | canvas resize is automatic |
| Association | swapchain is tied to one queue at creation | present queue family negotiated | one queue |

The RHI hides this as: the caller owns a surface; the RHI owns the images and
recycles them; acquire and present are explicit calls so a backend can block in
the right place. WebGPU's implicit acquire becomes a returned index the caller
must use for that frame only.

## 7. Boundary conventions

This is where a renderer most often breaks, because the three APIs disagree and
none of them agrees with a right-handed, reverse-Z engine.

| Convention | Engine and RHI | D3D12 | Vulkan | WebGPU | Metal |
|---|---|---|---|---|---|
| Clip-space Y | +Y up | +Y down | +Y down (core), GL is +Y up | +Y up | +Y up |
| NDC depth | reverse-Z, near = 1, far = 0, range 0..1 | 0..1 | 0..1 core (GL was -1..1) | 0..1 | -1..1 |
| Texture origin | top-left | top-left | top-left | top-left | bottom-left |
| Handedness | right-handed view, column-major matrices | any (app's) | any | any | any |

Consequences the backends must implement:

- **Y.** D3D12 and Vulkan need the Y flip. Doing it in the projection matrix
  costs nothing and keeps viewport coordinates identical on every backend; using
  a negative viewport height in Vulkan is the alternative and is what a backend
  with a prebuilt projection would use. The RHI picks the projection flip because
  the engine owns its matrices.
- **Depth.** Reverse-Z with a 0..1 range is native on D3D12, Vulkan, and WebGPU.
  Metal needs `depthClipMode`/a projection remap from -1..1 to 0..1, or a
  viewport depth range; that is a Metal-backend concern recorded here so the
  boundary can stay 0..1.
- **Texture origin.** A bottom-left API flips V in the sampler's address
  transform, not in the loader. The RHI's origin constant is the single source of
  truth, and a format/loader test asserts it.
- **Matrix storage.** Column-major with column vectors, shipped to the GPU as
  the same bytes; Vulkan's `mat4` in a uniform buffer is column-major too, D3D12
  is row-major by default and needs `#pragma pack_matrix(column_major)` or a
  transpose at upload. That is decided once, in the shader constant path of
  Phase 12.

## 8. Errors and validation

| Concept | D3D12 | Vulkan | WebGPU |
|---|---|---|---|
| Return convention | `HRESULT`, checked with `FAILED` | `VkResult` | exceptions or rejected promises |
| Device loss | `DXGI_ERROR_DEVICE_REMOVED`, `GetDeviceRemovedReason`, DRED | `VK_ERROR_DEVICE_LOST` | `device.lost` promise |
| Validation | `ID3D12Debug` layer, GPU-based validation, DRED | `VK_LAYER_KHRONOS_validation`, best-practices layer | always on, error scopes |
| Object naming | `SetPrivateData` with `WKPDID_D3DDebugObjectName` | `VK_EXT_debug_utils` labels | `label` on every descriptor |
| Out-of-memory | `E_OUTOFMEMORY` | `VK_ERROR_OUT_OF_DEVICE_MEMORY` / `..._HOST_MEMORY` | uncaptured error |

The RHI has no exceptions, so every call that can fail returns `Result` or
`Status` with a `core::Error`, and device loss is a distinct code so a caller can
distinguish "your arguments are wrong" from "the GPU is gone". Validation is the
RHI's own null-backend checks plus the platform layer in debug builds; a backend
maps its native validation messages into the same `CG_LOG` channel so a
caller does not need three different debug setups.

## 9. The common subset the RHI adopts

1. One adapter, one device, one queue per class (graphics, compute, copy).
2. Command recording into a reusable list per frame, reset before recording and
   immutable after submit.
3. Resources created from plain descriptors, referred to by generation-counted
   handles, with a memory class (device-local, upload, readback).
4. Bindings as immutable groups of typed slots, with the layout created first.
5. Explicit resource-state transitions and timeline fences.
6. A surface the caller owns; the RHI owns the swapchain images.
7. One convention set at the boundary: +Y up, reverse-Z 0..1, top-left texture
   origin, right-handed view, column-major matrices.

Everything else — sparse resources, mesh shaders, conservative raster, variable
rate shading, descriptor indexing — is a declared optional feature that a
backend may report and a caller may ask for, never a required part of the
interface.

## Sources

- Microsoft, *Direct3D 12 programming guide*: "Understanding Direct3D 12",
  "Work submission", "Memory management", "Resource binding", "Multi-engine
  synchronization", and the `ID3D12Fence`, `ID3D12Heap`,
  `D3D12_RESOURCE_BARRIER`, and `DXGI_SWAP_CHAIN_DESC1` references.
- The Khronos Group, *Vulkan 1.3 specification*: chapters "Devices and Queues",
  "Synchronization and Cache Control", "Command Buffers", "Descriptor Sets",
  "Memory Allocation", and the `VK_KHR_swapchain`, `VK_KHR_timeline_semaphore`,
  and `VK_KHR_dynamic_rendering` extension specifications.
- W3C, *WebGPU specification* (Candidate Recommendation): `GPUDevice`,
  `GPUQueue`, `GPUCommandEncoder`, `GPUTexture`, `GPUBindGroup`, `GPUSurface`.
- Apple, *Metal Shading Language* and the `MTLDevice`/`MTLRenderCommandEncoder`
  references, for the clip-space and texture-origin differences.
- Fabian Giesen, "A trip through the Graphics Pipeline 2011", part 1, for the
  runtime/user-mode driver/kernel driver split and command submission model.
