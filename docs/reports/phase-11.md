# Phase 11 report: resources

Date: 2026-10-25
Machine: Windows 11, 13th Gen Intel Core i7, MSVC 19.44, CMake 3.31
Design note: [phase-11-resources.md](../design/phase-11-resources.md)
ADR: [0014](../adr/0014-resource-model.md)
Format table: [formats.md](../formats.md)
Contract: [`cyber_rhi`](../../engines/cyber_rhi/docs/MODULE.md)

## Done-when

> The null backend passes tests for creation, lifetime, and invalid-use
> detection.

Proved in three parts:

- **Creation** — every descriptor rule has a case that asserts both the rejection
  and the error code: usage sets, extents, dimensions, cubes, arrays, mip chains,
  multisampling, limits, format capability, view compatibility, sampler LOD and
  anisotropy.
- **Lifetime** — destroying a texture retires every view of it; a recycled slot
  never resolves an old handle; destroying the device with live resources leaves
  the allocator at zero used bytes.
- **Invalid use** — writes to a device-local buffer, reads from one, out-of-range
  and overflowing host ranges, stale handles on every entry point, barriers whose
  source does not match tracked state, and barriers naming a state the resource
  kind cannot have.

`cyber_rhi` grows from 2,550 to 7,029 lines including tests, and from 7 to 12 test
binaries: 63 of 63 in debug, release, shipping, and ASan.

## What was built

| Header | Contents |
|---|---|
| `buffer.h` | `BufferUsage` (vertex, index, uniform, storage, indirect, copy, host-visible, readback), set helpers, `BufferDesc`, `BufferInfo`. |
| `texture.h` | `TextureDimension`, `TextureUsage`, `TextureAspect`, `SubresourceRange`, `TextureDesc`, `TextureViewDesc`, `TextureInfo`, `TextureViewInfo`, `MaxMipLevels`, `TextureByteSize`, `kTextureRowAlignment`. |
| `sampler.h` | `FilterMode`, `AddressMode`, `BorderColor`, `CompareFunction`, `SamplerDesc`, `kMaxMipLodBias`, `kMaxSamplerLod`. |
| `resource_state.h` | `ResourceState` as a bitmask, read/write/valid-set classification, buffer and texture state membership, `IsAspectValidForFormat`, `ResolveSubresourceRange`, `BufferBarrier`, `TextureBarrier`. |
| `format.h` (extended) | `FormatInfo`, `FormatInfoOf`, per-field accessors, `IsColorFormat`, sRGB/linear variant lookup, view compatibility, multisample, render-target, storage, and blend support. |

`backend.h` gains sixteen entries in `DeviceInterface`. `device.h` gains the
matching methods: `CreateBuffer`, `DestroyBuffer`, `BufferInfoOf`, `WriteBuffer`,
`ReadBuffer`, `BufferStateOf`, `CreateTexture`, `DestroyTexture`,
`TextureInfoOf`, `CreateTextureView`, `DestroyTextureView`,
`TextureViewInfoOf`, `TextureStateOf`, `CreateSampler`, `DestroySampler`, and
`Transition`.

Private implementation: `null_backend.cpp` grows from 448 to roughly 1,600 lines
— validation, four `SlotMap`-backed object tables, per-subresource state arrays,
an allocation list that outlives individual resources, and view retirement.

### What the interface implements today

Buffers, textures, views, and samplers are fully created, validated, queried,
and destroyed. Host-visible and readback buffers really allocate bytes, so
`WriteBuffer` then `ReadBuffer` round-trips. Transitions are validated and
recorded; they are not submitted to a queue, because there is no command list
yet. Texture data cannot be uploaded, because a copy command is Phase 13.

### Example

```cpp
cyber::rhi::BufferDesc stagingDesc{};
stagingDesc.usage = cyber::rhi::BufferUsage::HostVisible |
                    cyber::rhi::BufferUsage::CopySource;
stagingDesc.sizeBytes = 64 * 1024;

auto staging = device.CreateBuffer(stagingDesc);
device.WriteBuffer(staging.Value(), 0, cyber::memory::Span<const cyber::memory::Byte>(
                                          bytes, byteCount));

cyber::rhi::TextureDesc depthDesc{};
depthDesc.format = cyber::rhi::Format::D32Float;
depthDesc.usage = cyber::rhi::TextureUsage::DepthStencil |
                  cyber::rhi::TextureUsage::Sampled;
depthDesc.width = 1920;
depthDesc.height = 1080;

auto depth = device.CreateTexture(depthDesc);

cyber::rhi::TextureBarrier barrier{};
barrier.texture = depth.Value();
barrier.from = cyber::rhi::ResourceState::Undefined;
barrier.to = cyber::rhi::ResourceState::ShaderRead;

const cyber::rhi::TextureBarrier barriers[] = {barrier};
device.Transition({}, barriers);
```

## Decisions

- **Aggregate descriptors, no builders.** A zeroed descriptor is invalid where a
  resource has required fields; a sampler's default is the useful trilinear,
  repeat sampler. Rationale and alternatives in ADR 0014.
- **Host visibility is a usage flag.** `HostVisible` and `Readback` are mutually
  exclusive and cannot combine with a pipeline usage, which is the D3D12 heap
  rule. `WriteBuffer` requires `HostVisible`; `ReadBuffer` accepts either,
  because both heaps are CPU-mappable.
- **One `FormatInfo` table.** Every classification predicate is now a projection
  of it. A test walks all 28 enumerators, so a new format with no row fails.
- **One state vocabulary with three rules.** Reads combine, one write at most,
  `CopyDestination` never with a read. `Undefined` as a source discards; as a
  target it is rejected.
- **A mismatch is an error, not a coercion.** The barrier must name the tracked
  state; the null backend reports `InvalidArgument` rather than assuming the
  caller meant the other state.
- **Batch validation is atomic.** One bad barrier in a batch leaves every
  resource untouched.
- **Views borrow; the texture retires them.** A stale view handle is
  `InvalidArgument`, never a pointer into freed state.
- **The whole per-device table is checked at creation.** A backend with a missing
  entry fails device creation instead of failing the first call at run time.

## Evidence

All commands ran on the machine named above.

| Check | Result |
|---|---|
| `scripts/build.ps1 -Preset debug` | pass |
| `scripts/build.ps1 -Preset release` | pass |
| `scripts/build.ps1 -Preset shipping` | pass |
| `scripts/build.ps1 -Preset debug-asan` | pass |
| `ctest` debug / release / shipping / debug-asan | **63/63 each** — 58 existing plus 5 new binaries |
| `scripts/check_structure.py` | 16 modules, 1 contract |
| `scripts/layer_check.py` | 16 modules |
| `scripts/check_format.py` | 224 files |
| `scripts/verify_package.ps1` | installed consumer configures, links, runs — exit 0 |
| source consumer | configures, links, runs — exit 0 |

The ASan runtime DLL is still not on `PATH` in a bare shell, so `build/debug-asan`
needs `VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64` prepended; with it, 63/63 pass.

New test binaries:

| Test | Cases | What it proves |
|---|---|---|
| `rhi_format_table` | 5 | Every enumerator has a row with non-zero bytes, valid block extent, and consistent flags; explicit rows for all 28 formats; accessors; view compatibility in both directions; sRGB/linear variants are inverses; multisample, target, storage, and blend rules. |
| `rhi_buffer` | 6 | Create/info/destroy; every invalid usage combination, stride, and the size limit with distinct codes; host write and read round-trip including offsets, overwrites, the last byte, empty spans, null sources, and a 64-bit overflow; host access requires the right usage; stale handles do not alias a recycled slot; device teardown frees host bytes. |
| `rhi_texture` | 8 | Create/info and resolved view ranges; every invalid shape (extents, 1D/2D/3D rules, cube squareness and layer multiples, mip chain, sample counts, usage/format contradictions); limits with `OutOfRange`; views including sRGB compatibility, range bounds, dimensions, aspects, cube views, stale texture; view death with its texture and slot reuse; per-subresource state queries; storage texture rules; byte-size arithmetic and mip counts. |
| `rhi_sampler` | 4 | Defaults and lifetime; anisotropy range and the linear-filter requirement; LOD bounds, bias, and NaN rejection; every enum range and comparison. |
| `rhi_resource_state` | 6 | Classification and valid-set rules; buffer and texture state membership; buffer transitions and mismatches; texture ranges, per-subresource independence, and atomic batch rejection; a mixed batch changes nothing; range resolution including zero counts and invalid aspects. |

Allocator accounting is asserted after the buffer, texture, sampler, and view
tests: `HeapAllocator::Stats().used` is zero, so the null backend leaks neither
host bytes nor state arrays.

The installed consumer and the source consumer create a host-visible buffer,
write and read it back, create a texture and an sRGB view, create an anisotropic
sampler, transition the texture, verify a wrong-source barrier is rejected, and
destroy everything — through the exported `cyber::rhi` target only.

## Known gaps

- **No submission.** Barriers are validated and recorded; a queue has no command
  list to carry them until Phase 13. The missing-barrier test the roadmap asks
  for is therefore Phase 13's, and this phase supplies the state model it runs on.
- **No texture upload.** Phase 13's copy commands. `WriteBuffer` exists because a
  buffer can be host-visible without a copy; a texture cannot.
- **Destruction is immediate.** Nothing is submitted, so nothing can be in flight.
  Phase 13 adds retirement queues behind the same public API.
- **No GPU memory, aliasing, or sub-allocation.** Phase 15. Every resource is a
  whole allocation, and `TextureInfo::sizeBytes` is the null backend's arithmetic,
  not a driver's.
- **No typeless formats or depth-stencil reinterpretation.** A D24S8 texture has a
  D24S8 view. Phase 14 adds the rules its adapter reports.
- **`Unsupported` is unreachable locally for the two features it guards.** The
  null backend reports storage textures and anisotropic filtering so the valid
  paths are testable; the absent-feature branch is exercised by the D3D12
  conformance tests in Phase 14. The malformed-format branches are tested here.
- **Static error messages.** `core::Error` holds a non-owning pointer to a static
  string, so a message cannot interpolate a value. Each rule has its own message;
  formatted diagnostics wait for Phase 15's error sink.
- **`format.ps1` is non-idempotent on one pre-existing lambda.** Running it
  rewrote a lambda in `software_renderer/src/raster.cpp` into a form
  `check_format.py` then rejected; the file was restored to HEAD, which passes.
  The three new RHI files are unaffected. Worth a formatter-config investigation,
  not part of this phase.
- **The stray `diagnosing-low-phase-6-sample-frame-rate.json` at the repository
  root is still untracked.** Left alone; it is not part of this phase.

## Sources

- Microsoft, *D3D12 resource barriers* and *D3D12_HEAP_TYPE*: the state
  vocabulary, the copy-destination combination rule, and the upload/readback
  split.
- Khronos, *Vulkan 1.3 specification*, chapters 7 and 12: subresource ranges,
  aspect masks, image usage combinations, and sampler anisotropy.
- WebGPU candidate recommendation: `GPUTextureDescriptor`, `GPUBufferUsage`, and
  host-visible rules.
- Apple, *Metal Shading Language* and `MTLTextureDescriptor`: cube and array
  rules.
- Akenine-Möller et al., *Real-Time Rendering*, 4th edition, chapter 23: mip
  chains, block compression, and anisotropic filtering.

## What the next phase needs from this one

- `DeviceInterface` is the only backend surface; Phase 12 adds pipeline and
  binding entries to the same table rather than a parallel one.
- `Format` and `FormatInfo` are what a pipeline's attachment and vertex formats
  validate against, and what the D3D12 backend maps to `DXGI_FORMAT`.
- `resource_state.h` is the vocabulary a render pass and, later, the render graph
  use to declare what a pass reads and writes.
- The null backend's validation style — one static message per rule, one test
  that asserts the code — is the pattern the pipeline and binding tests follow.
- `TextureViewDesc` is the shape a bind group will name, so Phase 12 does not
  invent a second view type.
