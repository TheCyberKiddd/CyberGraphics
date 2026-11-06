# Phase 19 design — constant streaming and bindless resources

Date: 2026-11-06
Status: in progress
Related: [Phase 19 work package](../graphics/CYBERGRAPHICS_BUILD_GUIDE.md#phase-19-constant-streaming-and-bindless-resources), [ADR 0022](../adr/0022-frame-streaming-and-bindless-resources.md), [Phase 18 material design](./phase-18-materials.md)

## Goal

Replace per-instance GPU upload allocation for high-volume draw data with
fence-safe, aligned per-frame streaming; support per-draw offsets and
structured object data; provide reusable persistent and per-frame transient
descriptor indices; and expose bindless texture/buffer arrays only when the
adapter reports the required capability. Demonstrate and measure 10,000 unique
transforms, retaining a conventional binding path when bindless indexing is not
available.

## RHI binding contract

- Add an opt-in dynamic-offset property to a uniform, writable-storage, or
  read-only-storage buffer layout entry. `StorageBuffer` is writable and maps
  to a UAV; `ReadOnlyStorageBuffer` maps to an SRV. Dynamic entries have one
  resource; arrays remain ordinary descriptor arrays. A bind group stores a
  base offset and non-zero range. Each
  `SetBindGroup` call supplies one offset for each dynamic entry in layout
  order. The effective range is `baseOffset + dynamicOffset .. + rangeSize`;
  every addition is overflow-checked and must remain inside the buffer.
- Validate uniform and storage dynamic offsets against their respective device
  alignment limits and structured-element stride. Where both constraints
  apply, the offset satisfies both rather than assuming one alignment subsumes
  the other. Invalid counts, types, alignments, or ranges fail before
  recording. The null backend and validating wrapper exercise the same API.
- On D3D12, dynamic CBV/SRV/UAV entries use root descriptors and a root
  parameter recorded by slot and layout-entry index. Static entries continue
  to use descriptor tables. Reject root signatures that exceed the D3D12 root
  signature budget explicitly. This avoids per-draw descriptor-heap writes.
- Bindless tables use finite, reflected descriptor arrays. Updating a table
  writes its existing descriptor range; callers update only a frame slot whose
  completion fence has passed. `Feature::BindlessResources` reports queried
  adapter support rather than an assumed platform capability. Resource-array
  and sampler-array capacities are reported separately from binding-slot
  counts, so a supported backend can expose arrays larger than its number of
  layout entries.

## Renderer streaming and descriptor ownership

- Add a renderer-owned frame buffer stream over the existing RHI `FrameRing`.
  Each frame slot has a host-visible copy-source staging buffer and a
  device-local uniform/storage destination. `BeginFrame` accepts only a valid
  frame slot and resets that slot's cursor after the caller has waited for its
  fence. Allocations use checked alignment and bounds, copy caller bytes into
  staging immediately, and return the destination buffer plus byte offset.
- `RecordUploads` records only the written range, transitions the destination
  to the requested shader-read state, and clears dirty ranges only after all
  recording succeeds. The caller submits upload and consuming graphics work in
  queue order and does not reuse or destroy slot resources before completion.
- Persistent descriptor handles contain an index and generation and are
  allocated from a free list. Per-frame transient descriptor indices are
  allocated linearly and reset only when their `FrameSlot` is safe to reuse.
  Stale, double-freed, exhausted, and out-of-range operations return explicit
  errors.
- Bindless tables own fixed-size texture and storage-buffer arrays initialized
  with caller-supplied valid fallback resources. Allocated logical indices map
  directly to shader array indices. Freeing an entry restores its fallback
  descriptor. When the feature is absent, callers use the ordinary per-draw
  bind-group path; no bindless shader or RHI object is created on that path.
- Material definitions remain unchanged. A streamed material path uploads
  resolved instance parameter blocks through the frame stream; the existing
  per-instance upload remains available for low-volume and compatibility use.

## Demonstration and measurements

- `samples/08_frame_streaming` renders and submits 10,000 deterministic draws
  with unique transforms using the dynamic-offset path and, when supported, a
  structured bindless buffer table. The D3D12 sample uses the same scene,
  render target, draw count, warm-up, and sampling policy for both paths.
- The sample reports CPU recording and queue submission separately, plus
  CPU-observed frame completion latency. It records adapter, build
  configuration, frame budget, warm-up, sample count, median, and p95 to
  `phase19-submission-report.txt`. Completion latency is not presented as a GPU
  timestamp.
- Unit tests cover alignment, overflow, capacity, frame-slot reuse, write and
  upload ranges, descriptor free-list reuse and generations, transient reset,
  bindless feature selection, fallback selection, and dynamic-offset validation.
- The sample selects the first D3D12 adapter by default; enumeration places
  hardware adapters before WARP. Release runs on the Intel HD Graphics 530
  measured bindless p95 completion at 11,594 µs, 8,630 µs, and 7,854 µs over
  three independent ten-frame batches, below the 16,667 µs budget. The
  dynamic-offset comparison exceeds budget on this driver; WARP also exceeds
  budget. The bindless path meets the done-when criterion on this supported
  adapter, while the slower comparison remains explicitly documented. See the
  [Phase 19 report](../reports/phase-19.md).

## Constraints and sources

No third-party runtime dependency or copied implementation is introduced.
D3D12 shader-visible descriptor heaps and descriptor-table ranges follow the
official [Microsoft descriptor heaps guide](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps-overview).
Descriptor arrays and feature-dependent indexing follow the
[Vulkan descriptor set and indexing specification](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#descriptorsets).
CyberGraphics implements only the RHI surface needed by its current backends;
it does not add Vulkan headers or a Vulkan backend.
