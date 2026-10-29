#pragma once

#if defined(CYBER_RHI_HAS_D3D12)

#include "d3d12_com.h"
#include "gpu_allocator.h"

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/alignment.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/texture.h>

#include <condition_variable>
#include <mutex>

namespace cyber::rhi::detail
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::DynamicArray;

// Descriptors each attachment heap holds. A frame needs one per attachment; this
// is far more, so a device never recycles an RTV a live view still names.
inline constexpr u32 kAttachmentDescriptors = 256;

// The device is declared later: a command list holds a pointer back to it so the
// list's interface, which only ever receives the list, can reach the tables.
struct D3D12Device;

// Highest subresource count one texture may have. The state table is per
// (mip, layer) because D3D12 cannot express a per-aspect barrier; Phase 15
// replaces this flat table with an allocator once textures are transient.
inline constexpr u32 kMaxTrackedSubresources = 64;

// Slots kept in each pool. Handles index a slot and carry its generation, so a
// destroyed object's handle never resolves to its replacement.
struct D3D12FenceSlot
{
    ID3D12Fence* fence = nullptr;
    u64 value = 0;
    bool alive = false;
    u32 generation = 0;
};

struct D3D12Queue
{
    ID3D12CommandQueue* queue = nullptr;
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    u64 signalled = 0;
    u64 submissions = 0;
    // What the last accepted batch contained, for `submissionInfo`.
    SubmissionInfo last{};
};

struct D3D12BufferSlot
{
    ID3D12Resource* resource = nullptr;
    // Persistent mapping for an upload or readback heap; null for a default heap.
    void* mapped = nullptr;
    BufferDesc desc{};
    ResourceState state = ResourceState::Undefined;
    // The API's own state. It differs from `state` for host-visible and readback
    // heaps, whose state D3D12 fixes at creation.
    D3D12_RESOURCE_STATES native = D3D12_RESOURCE_STATE_COMMON;
    // Where the resource is placed (ADR 0018). Every buffer is a placed
    // resource: inside a sub-heap of its memory class, or on a dedicated heap
    // when no sub-heap could serve its size. `MemoryStats` and Phase 15's
    // aliasing checks read these back at destroy time.
    ID3D12Heap* heap = nullptr;
    u64 heapOffset = 0;
    // The reservation the allocator returned, with alignment padding included;
    // returning anything else would corrupt a neighbour.
    u64 reservedBytes = 0;
    d3d12::MemoryClassType memoryClass = d3d12::MemoryClassType::DeviceLocal;
    bool dedicated = false;
    bool alive = false;
    u32 generation = 0;
};

struct D3D12TextureSlot
{
    ID3D12Resource* resource = nullptr;
    TextureDesc desc{};
    ResourceState states[kMaxTrackedSubresources]{};
    D3D12_RESOURCE_STATES native[kMaxTrackedSubresources]{};
    // A presentable image belongs to its swapchain: the two texture entries that
    // would release it are refused, because the surface outlives any one image.
    bool swapChainOwned = false;
    bool alive = false;
    u32 generation = 0;
};

// Which descriptor heap a view's index refers to. Render-target and depth views
// cannot live in the shader-visible heap, so each kind has its own.
enum class DescriptorHeapKind : cyber::core::u8
{
    Resource = 0,
    RenderTarget,
    DepthStencil,
};

struct D3D12ViewSlot
{
    TextureHandle texture{};
    TextureViewDesc desc{};
    DescriptorHeapKind heap = DescriptorHeapKind::Resource;
    // Index into the device's shader-visible view heap.
    u32 descriptorIndex = 0;
    bool alive = false;
    u32 generation = 0;
};

struct D3D12SamplerSlot
{
    SamplerDesc desc{};
    u32 descriptorIndex = 0;
    bool alive = false;
    u32 generation = 0;
};

// Most entries one bind group layout may declare. The RHI's limit is the same
// number, so a layout that exceeds it is refused before it reaches here.
inline constexpr u32 kMaxLayoutEntries = 64;

// Most vertex buffers one list may have bound. This is the API's own slot count.
inline constexpr u32 kMaxVertexBindings = 32;

struct D3D12ShaderSlot
{
    // Compiled bytecode, owned. HLSL is compiled at creation, so what is stored
    // here is always a binary blob.
    void* code = nullptr;
    u64 codeSize = 0;
    ShaderStage stage = ShaderStage::Vertex;
    ShaderBytecodeFormat format = ShaderBytecodeFormat::Dxil;
    bool alive = false;
    u32 generation = 0;
};

struct D3D12LayoutSlot
{
    BindGroupLayoutEntry entries[kMaxLayoutEntries]{};
    u32 entryCount = 0;
    // Descriptors this layout occupies: the sum of every array count.
    u32 descriptorCount = 0;
    // The same total split by heap. A sampler descriptor cannot live in the
    // CBV/SRV/UAV heap, so a group is two blocks rather than one, and each block
    // is a separate table in the root signature.
    u32 resourceCount = 0;
    u32 samplerCount = 0;
    bool alive = false;
    u32 generation = 0;
};

struct D3D12GroupSlot
{
    BindGroupLayoutHandle layout{};
    // First descriptor of this group's resource block in the shader-visible heap,
    // and how many it spans.
    u32 descriptorIndex = 0;
    u32 descriptorCount = 0;
    // First descriptor of its sampler block in the sampler heap, and how many.
    u32 samplerIndex = 0;
    u32 samplerCount = 0;
    bool alive = false;
    u32 generation = 0;
};

// Pipeline state, fixed at creation. `hash` is the stable hash of the
// description the state was built from, which is also the key the pipeline cache
// stores it under.
struct D3D12PipelineSlot
{
    ID3D12PipelineState* state = nullptr;
    PipelineKind kind = PipelineKind::Graphics;
    u64 hash = 0;
    // Size of the pipeline's vertex layout, kept because a vertex buffer view
    // needs the stride and the pipeline is what declares it.
    u32 vertexBindingCount = 0;
    u32 vertexStrides[kMaxVertexBindings]{};
    // What the pipeline was built for, so a list can refuse to bind it into a
    // pass whose attachments or sample count differ.
    PipelineLayoutHandle layout{};
    u32 sampleCount = 1;
    u32 colorAttachmentCount = 0;
    Format colorFormats[kMaxColorAttachments]{};
    Format depthStencilFormat = Format::Count;
    bool alive = false;
    u32 generation = 0;
};

// A pipeline library, which is how D3D12 persists compiled pipelines across
// runs. Null when the adapter or the SDK version cannot provide one; the cache
// still exists and simply remembers nothing, which the contract allows.
struct D3D12PipelineCacheSlot
{
    ID3D12PipelineLibrary* library = nullptr;
    bool alive = false;
    u32 generation = 0;
};

// No root parameter: a slot uses no descriptors, or a range it needs is absent.
inline constexpr u32 kNoRootParameter = 0xFFFFFFFFU;

struct D3D12PipelineLayoutSlot
{
    ID3D12RootSignature* rootSignature = nullptr;
    // Root parameter index of each bind-group slot's resource table and sampler
    // table, or `kNoRootParameter`. Root parameter order is a property of the
    // signature, not of the slot number, so it is recorded rather than derived.
    u32 resourceParameter[kBindGroupSlotCount]{};
    u32 samplerParameter[kBindGroupSlotCount]{};
    // Descriptor count per slot, so a bind group can be checked against the
    // layout it will be bound through.
    u32 descriptorCounts[kBindGroupSlotCount]{};
    u32 pushConstantBytes = 0;
    // Root parameter index of the push-constant range, or `kNoRootParameter`.
    u32 pushConstantParameter = kNoRootParameter;
    bool alive = false;
    u32 generation = 0;
};

// One recording session. The allocator and the list are created together and
// reset together, so a list never holds a reset allocator's stale state.
struct D3D12CommandListSlot
{
    // The device the list records against. A list's interface only ever receives
    // its own state, so the way back to the tables it resolves handles in is here.
    D3D12Device* device = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    QueueKind queue = QueueKind::Graphics;
    CommandListState state = CommandListState::Initial;
    CommandListStats stats{};
    RenderPassInfo pass{};
    bool passOpen = false;
    // Depth of open debug labels, so `end` can refuse an unbalanced list.
    u32 labelDepth = 0;
    PipelineHandle pipeline{};
    PipelineKind pipelineKind = PipelineKind::Graphics;
    PipelineLayoutHandle layout{};
    BindGroupHandle groups[kBindGroupSlotCount]{};
    BufferHandle vertexBuffers[kMaxVertexBindings]{};
    u64 vertexOffsets[kMaxVertexBindings]{};
    bool vertexBound[kMaxVertexBindings]{};
    BufferHandle indexBuffer{};
    u64 indexOffset = 0;
    IndexFormat indexFormat = IndexFormat::Uint32;
    bool alive = false;
    u32 generation = 0;
};

// Images of one swapchain. The textures and views are entries in the device's
// own tables, marked `swapChainOwned` so destroying them through the texture
// entry is refused: the presentable surface owns their lifetime.
inline constexpr u32 kMaxSwapChainImages = 4;

struct D3D12SwapChainSlot
{
    // The device the images live in, because a swapchain's own interface only
    // ever receives this slot.
    D3D12Device* device = nullptr;
    IDXGISwapChain3* swapChain = nullptr;
    Format format = Format::BGRA8UnormSrgb;
    u32 width = 0;
    u32 height = 0;
    u32 imageCount = 0;
    PresentMode presentMode = PresentMode::Fifo;
    // Presented before the surface can be presented without tearing.
    bool allowsTearing = false;
    TextureHandle textures[kMaxSwapChainImages]{};
    TextureViewHandle views[kMaxSwapChainImages]{};
    bool acquired = false;
    u32 currentImage = 0;
    u64 presents = 0;
    bool alive = false;
    u32 generation = 0;
};  // What a device owns. Two devices of this backend share nothing: every pointer
// below hangs off `device`, and the one mutex guards the slot tables.
struct D3D12Device
{
    explicit D3D12Device(memory::Allocator& allocator) noexcept
        : fences(allocator), freeFenceSlots(allocator), buffers(allocator),
          freeBufferSlots(allocator), textures(allocator), freeTextureSlots(allocator),
          views(allocator), freeViewSlots(allocator), samplers(allocator),
          freeSamplerSlots(allocator), shaders(allocator), freeShaderSlots(allocator),
          layouts(allocator), freeLayoutSlots(allocator), groups(allocator),
          freeGroupSlots(allocator), pipelineLayouts(allocator), freePipelineLayoutSlots(allocator),
          pipelines(allocator), freePipelineSlots(allocator), pipelineCaches(allocator),
          freePipelineCacheSlots(allocator), commandLists(allocator),
          freeCommandListSlots(allocator), swapChains(allocator), freeSwapChainSlots(allocator),
          deferredReclaims(allocator), memory(allocator)
    {
    }

    D3D12Device(const D3D12Device&) = delete;
    D3D12Device& operator=(const D3D12Device&) = delete;

    ID3D12Device* device = nullptr;
    // The debug layer, when it was enabled. Released with the device.
    ID3D12Debug* debug = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    AdapterInfo adapterInfo{};
    D3D12Queue queues[kQueueKindCount]{};
    QueueHandle queueHandles[kQueueKindCount]{};
    Capabilities capabilities{};

    DynamicArray<D3D12FenceSlot> fences;
    DynamicArray<u32> freeFenceSlots;
    DynamicArray<D3D12BufferSlot> buffers;
    DynamicArray<u32> freeBufferSlots;
    DynamicArray<D3D12TextureSlot> textures;
    DynamicArray<u32> freeTextureSlots;
    DynamicArray<D3D12ViewSlot> views;
    DynamicArray<u32> freeViewSlots;
    DynamicArray<D3D12SamplerSlot> samplers;
    DynamicArray<u32> freeSamplerSlots;
    DynamicArray<D3D12ShaderSlot> shaders;
    DynamicArray<u32> freeShaderSlots;
    DynamicArray<D3D12LayoutSlot> layouts;
    DynamicArray<u32> freeLayoutSlots;
    DynamicArray<D3D12GroupSlot> groups;
    DynamicArray<u32> freeGroupSlots;
    DynamicArray<D3D12PipelineLayoutSlot> pipelineLayouts;
    DynamicArray<u32> freePipelineLayoutSlots;
    DynamicArray<D3D12PipelineSlot> pipelines;
    DynamicArray<u32> freePipelineSlots;
    DynamicArray<D3D12PipelineCacheSlot> pipelineCaches;
    DynamicArray<u32> freePipelineCacheSlots;
    DynamicArray<D3D12CommandListSlot> commandLists;
    DynamicArray<u32> freeCommandListSlots;
    DynamicArray<D3D12SwapChainSlot> swapChains;
    DynamicArray<u32> freeSwapChainSlots;

    // Shader-visible descriptors for views and samplers. One heap each because
    // D3D12 refuses to mix sampler and resource descriptors in one heap. The
    // first descriptor of each heap is scratch, for a clear that names an
    // attachment but has no view to reach it by.
    ID3D12DescriptorHeap* viewHeap = nullptr;
    u32 viewDescriptorSize = 0;
    u32 viewDescriptorsUsed = 1;
    ID3D12DescriptorHeap* samplerHeap = nullptr;
    u32 samplerDescriptorSize = 0;
    u32 samplerDescriptorsUsed = 1;
    // Attachment descriptors: the API forbids making these shader-visible, so
    // they have heaps of their own. Both reserve their first descriptor as
    // scratch, for a clear that names an attachment with no view to reach it.
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    u32 rtvDescriptorSize = 0;
    u32 rtvDescriptorsUsed = 1;
    ID3D12DescriptorHeap* dsvHeap = nullptr;
    u32 dsvDescriptorSize = 0;
    u32 dsvDescriptorsUsed = 1;
    // Submissions every queue has accepted, for `SubmissionInfo`.
    u64 submissions = 0;

    memory::Allocator* allocator = nullptr;
    u64 fenceSignals = 0;
    mutable std::mutex mutex;
    std::condition_variable condition;

    // Transient resources (ADR 0018, increment 2) whose reservation must not
    // return while the GPU may still be touching them: each record is a destroy
    // that waits until the tracker has passed its epoch before the bytes go
    // back to the allocator. Records leave in `DrainTransientReclaims`, which
    // create and destroy run, so `MemoryStats` always reads a clean answer.
    struct DeferredReclaim
    {
        d3d12::MemoryClassType memoryClass = d3d12::MemoryClassType::DeviceLocal;
        ID3D12Heap* heap = nullptr;
        u64 offset = 0;
        u64 reservedBytes = 0;
        bool dedicated = false;
        // Every submission before this epoch is one the bytes may still serve.
        u64 epoch = 0;
    };
    DynamicArray<DeferredReclaim> deferredReclaims;
    // Signalled at every submission. A transient destroy defers its return
    // only when this exists, has signalled at least once, and the destroy's
    // epoch is not complete yet.
    ID3D12Fence* transientTracker = nullptr;
    u64 transientEpoch = 0;

    // GPU memory the backend owns, sub-allocating inside large heaps (ADR 0018,
    // increment 1), instead of one heap per resource. Guarded by `mutex`, like
    // every other device member: a caller that shares the device across threads
    // does so through the same RHI calls the mutex already guards.
    // Declared last because it is constructed with `allocator`, which every
    // array above consumes first.
    d3d12::GpuHeapAllocator memory;
};

// Slot resolution. Every one returns null for a stale or default handle.
[[nodiscard]] D3D12FenceSlot* Resolve(D3D12Device& device, FenceHandle handle) noexcept;
[[nodiscard]] D3D12BufferSlot* Resolve(D3D12Device& device, BufferHandle handle) noexcept;
[[nodiscard]] D3D12TextureSlot* Resolve(D3D12Device& device, TextureHandle handle) noexcept;
[[nodiscard]] D3D12ViewSlot* Resolve(D3D12Device& device, TextureViewHandle handle) noexcept;
[[nodiscard]] D3D12SamplerSlot* Resolve(D3D12Device& device, SamplerHandle handle) noexcept;
[[nodiscard]] D3D12ShaderSlot* Resolve(D3D12Device& device, ShaderHandle handle) noexcept;
[[nodiscard]] D3D12LayoutSlot* Resolve(D3D12Device& device, BindGroupLayoutHandle handle) noexcept;
[[nodiscard]] D3D12GroupSlot* Resolve(D3D12Device& device, BindGroupHandle handle) noexcept;
[[nodiscard]] D3D12PipelineLayoutSlot* Resolve(D3D12Device& device,
                                               PipelineLayoutHandle handle) noexcept;
[[nodiscard]] D3D12PipelineSlot* Resolve(D3D12Device& device, PipelineHandle handle) noexcept;
[[nodiscard]] D3D12PipelineCacheSlot* Resolve(D3D12Device& device,
                                              PipelineCacheHandle handle) noexcept;
[[nodiscard]] D3D12CommandListSlot* Resolve(D3D12Device& device, CommandListHandle handle) noexcept;
[[nodiscard]] D3D12SwapChainSlot* Resolve(D3D12Device& device, SwapChainHandle handle) noexcept;

// Conversion between the RHI's vocabulary and D3D12's. Every one is total: an
// unknown enumerator maps to the API's conservative answer rather than asserting,
// because a descriptor is validated before it reaches this layer.
[[nodiscard]] DXGI_FORMAT ToDxgiFormat(Format format) noexcept;
[[nodiscard]] D3D12_RESOURCE_STATES ToD3D12State(ResourceState state) noexcept;
[[nodiscard]] ResourceState FromD3D12State(D3D12_RESOURCE_STATES state) noexcept;
[[nodiscard]] D3D12_RESOURCE_DIMENSION ToD3D12Dimension(TextureDimension dimension) noexcept;
[[nodiscard]] D3D12_COMPARISON_FUNC ToComparisonFunc(CompareFunction function) noexcept;
[[nodiscard]] D3D12_FILTER ToFilter(const SamplerDesc& desc) noexcept;
[[nodiscard]] D3D12_TEXTURE_ADDRESS_MODE ToAddressMode(AddressMode mode) noexcept;
[[nodiscard]] D3D12_HEAP_TYPE ToHeapType(BufferUsage usage) noexcept;
[[nodiscard]] D3D12_RESOURCE_FLAGS ToTextureFlags(TextureUsage usage) noexcept;

// Number of subresources a description produces, and the index of one.
[[nodiscard]] u32 SubresourceCount(const TextureDesc& desc) noexcept;
[[nodiscard]] u32 SubresourceIndex(const TextureDesc& desc, u32 mipLevel, u32 arrayLayer) noexcept;

// Submits `barriers` on the graphics queue through a one-shot list and waits for
// them, which is what a transition outside a recording needs. Returns
// `ErrorCode::Internal` when the list or its allocator cannot be created.
[[nodiscard]] Status<Error>
SubmitBarriers(D3D12Device& device, const D3D12_RESOURCE_BARRIER* barriers, u32 count) noexcept;

// CPU handle of a view, in whichever heap it was created in.
[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE DescriptorCpuHandle(D3D12Device& device,
                                                              const D3D12ViewSlot& view) noexcept;

// Fills the shader resource view a sampled view addresses. A bind group writes
// this view straight into its own descriptor instead of copying one out of the
// view's heap: D3D12 makes a shader-visible heap write-only for the CPU, so a
// copy that reads a descriptor back out of one is invalid.
void BuildSampledViewDesc(const D3D12TextureSlot& texture, const TextureViewDesc& desc,
                          D3D12_SHADER_RESOURCE_VIEW_DESC& out) noexcept;

// Fills the API's sampler description from the RHI's.
void BuildSamplerDesc(const SamplerDesc& desc, D3D12_SAMPLER_DESC& out) noexcept;

// Sets the debug name of an object from a UTF-8 label, when one is given.
void NameObject(ID3D12Object* object, const char* label) noexcept;

// Releases every resource slot the device owns. Called once, from destroy.
void ReleaseResources(D3D12Device& device) noexcept;

// Resource, view, and sampler entries of the device table, implemented in
// `d3d12_resources.cpp` because they outgrew the device file.
[[nodiscard]] Status<Error> D3D12CreateBuffer(void* internal, const BufferDesc& desc,
                                              BufferHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyBuffer(void* internal, BufferHandle buffer) noexcept;
[[nodiscard]] BufferInfo D3D12BufferInfo(const void* internal, BufferHandle buffer) noexcept;
[[nodiscard]] Status<Error> D3D12WriteBuffer(void* internal, BufferHandle buffer, u64 offset,
                                             memory::Span<const memory::Byte> bytes) noexcept;
[[nodiscard]] Status<Error> D3D12ReadBuffer(void* internal, BufferHandle buffer, u64 offset,
                                            memory::Span<memory::Byte> bytes) noexcept;
[[nodiscard]] ResourceState D3D12BufferState(const void* internal, BufferHandle buffer) noexcept;
// Deferred-reservation drain (ADR 0018, increment 2). `DrainTransientReclaims`
// returns the reservations the tracker has passed; create and destroy call it
// under the device mutex. `FlushTransientReclaims` returns every deferred
// reservation without waiting; teardown calls it because the device is leaving.
void DrainTransientReclaims(D3D12Device& device) noexcept;
void FlushTransientReclaims(D3D12Device& device) noexcept;
[[nodiscard]] Status<Error> D3D12CreateTexture(void* internal, const TextureDesc& desc,
                                               TextureHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyTexture(void* internal, TextureHandle texture) noexcept;
[[nodiscard]] TextureInfo D3D12TextureInfo(const void* internal, TextureHandle texture) noexcept;
[[nodiscard]] Status<Error> D3D12CreateTextureView(void* internal, const TextureViewDesc& desc,
                                                   TextureViewHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyTextureView(void* internal,
                                                    TextureViewHandle view) noexcept;
[[nodiscard]] TextureViewInfo D3D12TextureViewInfo(const void* internal,
                                                   TextureViewHandle view) noexcept;
[[nodiscard]] ResourceState D3D12TextureState(const void* internal, TextureHandle texture,
                                              u32 mipLevel, u32 arrayLayer) noexcept;
[[nodiscard]] Status<Error> D3D12CreateSampler(void* internal, const SamplerDesc& desc,
                                               SamplerHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroySampler(void* internal, SamplerHandle sampler) noexcept;
[[nodiscard]] Status<Error> D3D12Transition(void* internal,
                                            memory::Span<const BufferBarrier> buffers,
                                            memory::Span<const TextureBarrier> textures) noexcept;

// Shader, binding, and pipeline-layout entries, implemented in
// `d3d12_binding.cpp`.
[[nodiscard]] Status<Error> D3D12CreateShader(void* internal, const ShaderDesc& desc,
                                              ShaderHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyShader(void* internal, ShaderHandle shader) noexcept;
[[nodiscard]] ShaderInfo D3D12ShaderInfo(const void* internal, ShaderHandle shader) noexcept;
[[nodiscard]] Status<Error> D3D12CreateBindGroupLayout(void* internal,
                                                       const BindGroupLayoutDesc& desc,
                                                       BindGroupLayoutHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyBindGroupLayout(void* internal,
                                                        BindGroupLayoutHandle layout) noexcept;
[[nodiscard]] Status<Error> D3D12CreateBindGroup(void* internal, const BindGroupDesc& desc,
                                                 BindGroupHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyBindGroup(void* internal, BindGroupHandle group) noexcept;
[[nodiscard]] BindGroupInfo D3D12BindGroupInfo(const void* internal,
                                               BindGroupHandle group) noexcept;
[[nodiscard]] Status<Error> D3D12CreatePipelineLayout(void* internal,
                                                      const PipelineLayoutDesc& desc,
                                                      PipelineLayoutHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyPipelineLayout(void* internal,
                                                       PipelineLayoutHandle layout) noexcept;

// Pipeline state and pipeline caches, implemented in `d3d12_pipelines.cpp`.
[[nodiscard]] Status<Error> D3D12CreateGraphicsPipeline(void* internal,
                                                        const GraphicsPipelineDesc& desc,
                                                        PipelineHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyGraphicsPipeline(void* internal,
                                                         PipelineHandle pipeline) noexcept;
[[nodiscard]] Status<Error> D3D12CreateComputePipeline(void* internal,
                                                       const ComputePipelineDesc& desc,
                                                       PipelineHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyComputePipeline(void* internal,
                                                        PipelineHandle pipeline) noexcept;
[[nodiscard]] PipelineInfo D3D12PipelineInfo(const void* internal,
                                             PipelineHandle pipeline) noexcept;
[[nodiscard]] Status<Error> D3D12CreatePipelineCache(void* internal, const PipelineCacheDesc& desc,
                                                     PipelineCacheHandle* out) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyPipelineCache(void* internal,
                                                      PipelineCacheHandle cache) noexcept;
[[nodiscard]] Status<Error> D3D12PipelineCacheBytes(const void* internal, PipelineCacheHandle cache,
                                                    memory::Allocator& allocator,
                                                    DynamicArray<memory::Byte>* out) noexcept;

// Recording, submission, and presentation, implemented in `d3d12_commands.cpp`
// and `d3d12_swapchain.cpp`.
[[nodiscard]] Status<Error> D3D12CreateCommandList(void* internal, const CommandListDesc& desc,
                                                   CommandListHandle* out,
                                                   CommandListCreation* creation) noexcept;
[[nodiscard]] Status<Error> D3D12DestroyCommandList(void* internal,
                                                    CommandListHandle list) noexcept;
[[nodiscard]] Status<Error> D3D12Submit(void* internal, const SubmitDesc& desc) noexcept;
[[nodiscard]] SubmissionInfo D3D12SubmissionInfo(const void* internal, QueueKind kind) noexcept;
[[nodiscard]] Status<Error> D3D12CreateSwapChain(void* internal, const SwapChainDesc& desc,
                                                 SwapChainHandle* out,
                                                 SwapChainCreation* creation) noexcept;
[[nodiscard]] Status<Error> D3D12DestroySwapChain(void* internal,
                                                  SwapChainHandle swapchain) noexcept;

// Ends every recording and releases every list's allocator. Called once, from
// device teardown, after `releaseResources`.
void ReleaseCommandLists(D3D12Device& device) noexcept;

// Releases every swapchain's images. Called once, from device teardown.
void ReleaseSwapChains(D3D12Device& device) noexcept;

}  // namespace cyber::rhi::detail

#endif
