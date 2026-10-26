# Phase 11 — Resources: buffers, textures, views, samplers, and states

Date: 2026-10-25
Status: approved
Related: ADR 0006 (allocation and handles), ADR 0013 (RHI object model),
ADR 0014 (resource model and states),
[`cyber_rhi` contract](../../engines/cyber_rhi/docs/MODULE.md),
[format table](../formats.md), [Phase 10 note](phase-10-rhi.md)

## What this phase builds

Phase 10 fixed the object model and implemented nothing but devices and fences.
Phase 11 makes resources real: the memory a renderer actually reads from and
writes to, the description structs that create it, and the state machine that
keeps a backend honest about how it is being read.

- **Buffers.** Vertex, index, uniform, storage, indirect, copy, upload, and
  readback, described by a `BufferDesc` and reported by a `BufferInfo`.
- **Textures.** 1D, 2D, 3D, cube, and arrays; mip chains; multisampling;
  color, depth, and depth-stencil formats.
- **Texture views.** A typed window onto a texture — a subresource range and a
  view format that may differ from the base only in its sRGB encoding.
- **Samplers.** Filter, mipmap filter, address modes per axis, anisotropy, LOD
  clamp and bias, border color, and an optional depth comparison.
- **A format table.** Bytes per block, block extent, channel count, and
  component type for every enumerator, plus the view-compatibility and
  multisample rules a backend needs.
- **A resource state model.** One vocabulary that maps to D3D12 resource states
  and Vulkan image layouts, with transitions that are checked against tracked
  per-subresource state.

The roadmap done-when line is: *the null backend passes tests for creation,
lifetime, and invalid-use detection.* Everything below serves that: the null
backend is not a stub, it is the reference implementation of the rules a real
backend must also enforce, and its tests are the specification a D3D12 and a
Vulkan backend are measured against in Phases 14 and 34.

## Why the RHI owns this and not the renderer

The software renderer already has a `Framebuffer`, a `Texture`, and a pipeline.
None of them is reused here, and that is deliberate:

- A `Framebuffer` is a CPU allocation with a fixed layout. A GPU texture is a
  description plus a subresource layout that the backend chooses, and its bytes
  may not be addressable by the CPU at all.
- The software renderer's `Texture` stores mips in one allocation with a
  renderer-chosen layout. A backend must be free to tile, swizzle, or compress.
- The software renderer never needs a resource state, because it has no
  asynchronous consumer. A GPU does.

What *is* shared is the contract: `Format` names, the +Y-up / reverse-Z /
top-left-origin conventions, and the generation-counted handle rule. Phase 14
compares a GPU frame against the software renderer's output precisely because
both speak the same conventions.

## Interfaces

All new headers live in `engines/cyber_rhi/include/cyber/rhi/` and are
self-contained.

| Header | Contents |
|---|---|
| `buffer.h` | `BufferUsage`, `BufferDesc`, `BufferInfo`, `HasUsage`. |
| `texture.h` | `TextureDimension`, `TextureUsage`, `TextureAspect`, `SubresourceRange`, `TextureDesc`, `TextureViewDesc`, `TextureInfo`, `TextureViewInfo`, `MaxMipLevels`, `TextureByteSize`, `kTextureRowAlignment`. |
| `sampler.h` | `FilterMode`, `MipmapFilterMode`, `AddressMode`, `BorderColor`, `CompareFunction`, `SamplerDesc`. |
| `resource_state.h` | `ResourceState`, `ResourceStateName`, `BufferBarrier`, `TextureBarrier`, state classification, and the valid-state predicates. |
| `format.h` (extended) | `FormatInfo`, `FormatInfoOf`, per-field accessors, view compatibility, multisample and target support, sRGB/linear variant lookup. |

`backend.h` gains sixteen entries in `DeviceInterface`; `device.h` gains the
matching public methods. Nothing about the Phase 10 API changes meaning.

## Descriptors, not builder objects

Every creation call takes a plain aggregate by const reference and writes a
handle. There is no builder, no fluent chain, and no default-constructed hidden
state. The reasons are the ones that already govern this codebase: a struct is
trivially copyable, comparable, dumpable, and safe to fill from a serializer;
it is the shape the Phase 25 C ABI needs; and a caller can zero-initialize it
and set two fields without wondering what the defaults mean.

Defaults are chosen so that a zeroed descriptor is *invalid* rather than
silently something. `BufferDesc{}` has a zero size and no usage, so it fails
with `InvalidArgument`. `TextureDesc{}` has a zero width. `SamplerDesc{}` is
valid, because a sampler has no required field: the default is trilinear,
repeat, no anisotropy, no compare.

## The format table is the single source of truth

Before this phase, `format.h` answered "is this compressed?" in one switch and
"is this depth?" in another. Two switches over the same enum drift apart the
next time a format is added. Phase 11 replaces both with one `FormatInfoOf`
table and makes every predicate a projection of it, so depth-ness, byte size,
and block extent cannot disagree.

The table records:

- `bytesPerBlock` — bytes for one texel, or one 4x4 block for a BC format;
- `blockWidth`/`blockHeight` — 1 for uncompressed, 4 for BC;
- `channelCount` — 1 to 4, counting the depth and stencil channels as
  channels;
- `componentType` — `Unorm`, `Float`, `Uint`, `Depth`, or `DepthStencil`;
- `depth`, `stencil`, `srgb`, `compressed` — the classification flags.

The rendered table is in [formats.md](../formats.md), which is generated by
hand next to the header and checked by a test that walks every enumerator.

`TextureByteSize` applies a row alignment of 256 bytes by default, because that
is D3D12's `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` and a safe superset for Vulkan's
`VkPhysicalDeviceLimits::optimalBufferCopyRowPitchAlignment` on every
implementation this project targets. Vulkan's per-format alignment is smaller
in most cases, so aligning to 256 is never wrong for a copy, only larger.

## The resource state model

A state says how a resource is being read and by whom. The vocabulary is a
bitmask because D3D12 states combine and because a render graph will want to
describe "read as a shader resource *and* as a copy source" in one transition.

| State | Kind | Buffers | Textures |
|---|---|---|---|
| `Undefined` | none | discard only | discard only |
| `Present` | write | no | yes (swapchain, Phase 13) |
| `CopySource` | read | yes | yes |
| `CopyDestination` | write | yes | yes |
| `VertexBuffer` | read | yes | no |
| `IndexBuffer` | read | yes | no |
| `ConstantBuffer` | read | yes | no |
| `ShaderRead` | read | yes | yes |
| `ShaderWrite` | write | yes | yes |
| `RenderTarget` | write | no | yes |
| `DepthRead` | read | no | yes |
| `DepthWrite` | write | no | yes |
| `IndirectArgument` | read | yes | no |

Three rules make the model checkable:

1. **At most one write state per resource.** Two writers on one subresource is
   a race the caller must split with two barriers, never one combined state.
2. **`CopyDestination` cannot combine with a read state.** A copy destination
   produces contents; reading them in the same state is exactly the hazard a
   barrier exists to prevent.
3. **`Undefined` on the `from` side means "contents may be discarded".** It is
   the only way to transition a freshly created resource and the only `from`
   value accepted without matching tracked state. `Undefined` on the `to` side
   is always rejected.

Anything else on the `from` side must equal the tracked state of that
subresource, or the transition fails with `InvalidArgument` and a message that
names the mismatch. That is the check D3D12's debug layer performs and the
reason a missing barrier becomes a test failure in Phase 13 rather than a
corrupted frame in Phase 14.

State is tracked per subresource — one entry per `(mip, array layer)`. It is
*not* tracked per aspect. D3D12 barriers are per subresource, so a
depth-stencil texture has one state for the pair; Vulkan has a layout per
aspect, and Phase 34's backend derives the second layout from the one state it
was given. Splitting aspects in the RHI would force every caller to submit two
barriers for one attachment, which is the Vulkan model leaking through.

## Views own nothing, and die with their texture

A `TextureViewHandle` is a handle in its own right, with its own generation,
because every backend has a real object behind it (an SRV descriptor, a
`VkImageView`). It is created from a texture and a `SubresourceRange`, and it
borrows the texture.

Two lifetime rules follow, and both are tested:

- **Destroying a view invalidates that view and nothing else.** The texture
  stays usable.
- **Destroying a texture invalidates every view of it.** The backend does not
  hand back a view that now points at freed memory; the handle stops resolving
  exactly as if the caller had destroyed it. The null backend keeps a list of
  live views per texture and retires them together.

The second rule is why a view carries the texture handle inside its descriptor
rather than a pointer: the backend can re-derive ownership without a second
registry.

## Host-visible memory is a usage flag

An upload buffer is written by the CPU and read by the copy engine; a readback
buffer is written by the copy engine and read by the CPU. Rather than a memory
class enum that a backend must translate, Phase 11 states the capability
directly:

- `BufferUsage::HostVisible` — the CPU may `WriteBuffer` into it.
- `BufferUsage::Readback` — the CPU may `ReadBuffer` out of it.

An upload buffer is also CPU-readable, because D3D12's upload heap is mappable
and Vulkan's host-visible memory is readable; only a device-local buffer is not.
`WriteBuffer` therefore requires `HostVisible`, while `ReadBuffer` accepts
`HostVisible` or `Readback`.

They are mutually exclusive, and neither combines with a shader or pipeline
usage. That is not a simplification of D3D12; it is its rule. An upload heap
buffer is `D3D12_HEAP_TYPE_UPLOAD` and cannot be a vertex buffer without an
extra copy, and a readback buffer is `D3D12_HEAP_TYPE_READBACK` and cannot be
bound at all. Making the invalid combination a creation error catches the
mistake at the call site instead of at the first draw.

The null backend really allocates host memory for those two usages, so
`WriteBuffer` followed by `ReadBuffer` round-trips actual bytes. Every other
usage stores no bytes at all: the null backend is not pretending to be a GPU,
it is checking the contract.

## Validation order and error codes

Every creation path validates in the same order, so the first error a caller
sees is the most specific one:

1. **Device** — an empty device rejects everything with `InvalidArgument`.
2. **Handle** — a stale handle is `InvalidArgument`, never `NotFound`. The two
   cases a caller must distinguish are "no such object" (a bug) and "the GPU
   has not caught up" (`NotFound` from a fence timeout).
3. **Shape** — zero sizes, zero counts, and malformed ranges are
   `InvalidArgument`.
4. **Limits** — a value above `Capabilities::limits` is `OutOfRange`. A caller
   that reads capabilities can avoid it; one that guesses gets a distinct code.
5. **Capability** — a feature the adapter lacks is `Unsupported`. Phase 11
   exercises this with anisotropic filtering and storage textures.
6. **Allocation** — an allocator refusal is `Internal`, and the object is not
   half-created.

Messages are static strings. `core::Error` holds a non-owning `const char*`
with static lifetime by design, so a message cannot interpolate the offending
value without a buffer. Each rule therefore gets its own message, which is
enough for a test to assert the code and for a human to read the log. Formatted
diagnostics arrive with the validation layer in Phase 15, where an error sink
exists.

## Threading

`Device::CreateBuffer`, `DestroyBuffer`, `CreateTexture`, `DestroyTexture`,
`CreateTextureView`, `DestroyTextureView`, `CreateSampler`, `DestroySampler`,
`WriteBuffer`, `ReadBuffer`, and `Transition` are safe to call from any thread.
The null backend serializes them behind its existing device mutex. Object
creation on D3D12 and Vulkan is thread-safe as well; the mutex is what keeps
the null backend's own tables consistent, not a requirement the RHI imposes on
callers.

Recording is not thread-safe and will not be: command lists are single-threaded
in Phase 13, one per recording thread.

## Deferred destruction

The module contract says resources are destroyed with deferred destruction. In
Phase 11 nothing is ever submitted, so retirement is immediate: the handle is
invalidated at once and the backing bytes are released in the same call. The
public API does not change when Phase 13 adds submission — `DestroyBuffer`
still means "I am done with this handle" — and the backend will then hold the
allocation until the timeline passes the last submission that referenced it.
That is recorded here so the difference is a known implementation detail rather
than a surprise.

## What this phase deliberately leaves out

- **Pipelines, shader modules, bind groups** — Phase 12.
- **Command lists, barriers submitted on a queue, swapchains** — Phase 13.
  `Device::Transition` exists now so the state model can be tested and so the
  backend has one place to insert a barrier; submitting it is Phase 13's job.
- **Mapped persistent pointers and a staging ring** — Phase 19. Phase 11 offers
  `WriteBuffer`/`ReadBuffer` as immediate operations, which is what a loader
  needs and what a test can assert.
- **Typeless formats and depth-stencil reinterpretation** — Phase 14, when a
  backend's actual rules can be read from `CheckFeatureSupport`.
- **GPU memory allocation and aliasing** — Phase 15. Every resource here is a
  whole allocation.
- **Texture data upload** — Phase 13's copy commands. Phase 11 creates empty
  resources; `WriteBuffer` exists because a buffer can be host-visible without
  a copy, a texture cannot.

## Example

```cpp
cyber::memory::HeapAllocator heap;
auto created = cyber::rhi::Device::Create(desc, heap);
cyber::rhi::Device device = std::move(created.Value());

// A vertex buffer: GPU-read, never touched by the CPU after upload.
cyber::rhi::BufferDesc vertexDesc{};
vertexDesc.usage = cyber::rhi::BufferUsage::Vertex |
                   cyber::rhi::BufferUsage::CopyDestination;
vertexDesc.sizeBytes = 1024 * 1024;
vertexDesc.label = "static geometry";

auto vertexBuffer = device.CreateBuffer(vertexDesc);

// A staging buffer: CPU writes, copy engine reads.
cyber::rhi::BufferDesc stagingDesc{};
stagingDesc.usage = cyber::rhi::BufferUsage::HostVisible |
                    cyber::rhi::BufferUsage::CopySource;
stagingDesc.sizeBytes = 64 * 1024;

auto staging = device.CreateBuffer(stagingDesc);
device.WriteBuffer(staging.Value(), 0, cyber::memory::Span<const cyber::memory::Byte>(
                                          bytes, byteCount));

// A depth target with a full mip chain.
cyber::rhi::TextureDesc depthDesc{};
depthDesc.format = cyber::rhi::Format::D32Float;
depthDesc.usage = cyber::rhi::TextureUsage::DepthStencil |
                  cyber::rhi::TextureUsage::Sampled;
depthDesc.width = 1920;
depthDesc.height = 1080;
depthDesc.mipLevels = cyber::rhi::MaxMipLevels(cyber::rhi::TextureDimension::Dim2D,
                                              1920, 1080, 1);

auto depth = device.CreateTexture(depthDesc);

// A view that samples only mip 0 as a shader resource.
cyber::rhi::TextureViewDesc viewDesc{};
viewDesc.texture = depth.Value();
viewDesc.format = cyber::rhi::Format::D32Float;
viewDesc.range.baseMipLevel = 0;
viewDesc.range.mipLevelCount = 1;
viewDesc.range.aspect = cyber::rhi::TextureAspect::Depth;

auto sampledDepth = device.CreateTextureView(viewDesc);

// Move the whole texture to a read state before the shader samples it.
cyber::rhi::TextureBarrier barrier{};
barrier.texture = depth.Value();
barrier.range = viewDesc.range;
barrier.from = cyber::rhi::ResourceState::Undefined;
barrier.to = cyber::rhi::ResourceState::ShaderRead;

const cyber::rhi::TextureBarrier barriers[] = {barrier};
device.Transition({}, barriers);
```

## Verification

The phase is done when every rule above has a test that fails if the rule is
removed:

| Test binary | Proves |
|---|---|
| `rhi_format_table` | Every enumerator's bytes, block extent, channels, and component type; view compatibility; multisample and target support; byte-size arithmetic across mips, layers, and block formats. |
| `rhi_buffer` | Creation, usage validation, limits, host visibility, write/read round trip, bounds and overflow, stale handles, slot reuse, allocator cleanliness. |
| `rhi_texture` | 1D/2D/3D/cube/array/mip/MSAA creation, every invalid shape, view creation and format compatibility, view death with the texture, byte size. |
| `rhi_sampler` | Filter, address, border, LOD, anisotropy, and comparison validation, including the feature gate. |
| `rhi_resource_state` | State classification and combination rules, buffer and texture barrier validation, tracked-state mismatch, range resolution, and per-subresource independence. |

Plus the four-preset build, the structure/layer/format gates, and the
installed-package consumer exercising a buffer and a texture through the public
target.

## Sources

- Microsoft, *D3D12 resource barriers* and *D3D12_HEAP_TYPE*: the state
  vocabulary and the upload/readback split this note follows.
- Khronos, *Vulkan 1.3 specification*, chapters 7 (resources) and 12
  (synchronization): image layouts, subresource ranges, and aspect masks.
- WebGPU candidate recommendation, sections on `GPUTextureDescriptor` and
  `GPUBufferUsage`: the bitmask-usage shape and the host-visible rules.
- Apple, *Metal Shading Language* and `MTLTextureDescriptor`: 3D and array
  texture rules, and the absent geometry stage that makes a separate view
  necessary.
- Akenine-Möller et al., *Real-Time Rendering*, 4th edition, chapter 23:
  texture memory layout, mip chains, and anisotropic filtering.
