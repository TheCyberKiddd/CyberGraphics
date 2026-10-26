#pragma once

#if defined(CYBER_RHI_HAS_D3D12)

#include "d3d12_com.h"

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
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
    bool alive = false;
    u32 generation = 0;
};

struct D3D12TextureSlot
{
    ID3D12Resource* resource = nullptr;
    TextureDesc desc{};
    ResourceState states[kMaxTrackedSubresources]{};
    D3D12_RESOURCE_STATES native[kMaxTrackedSubresources]{};
    bool alive = false;
    u32 generation = 0;
};

struct D3D12ViewSlot
{
    TextureHandle texture{};
    TextureViewDesc desc{};
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

// What a device owns. Two devices of this backend share nothing: every pointer
// below hangs off `device`, and the one mutex guards the slot tables.
struct D3D12Device
{
    explicit D3D12Device(memory::Allocator& allocator) noexcept
        : fences(allocator), freeFenceSlots(allocator), buffers(allocator),
          freeBufferSlots(allocator), textures(allocator), freeTextureSlots(allocator),
          views(allocator), freeViewSlots(allocator), samplers(allocator),
          freeSamplerSlots(allocator)
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

    // Shader-visible descriptors for views and samplers. One heap each because
    // D3D12 refuses to mix sampler and resource descriptors in one heap.
    ID3D12DescriptorHeap* viewHeap = nullptr;
    u32 viewDescriptorSize = 0;
    u32 viewDescriptorsUsed = 0;
    ID3D12DescriptorHeap* samplerHeap = nullptr;
    u32 samplerDescriptorSize = 0;
    u32 samplerDescriptorsUsed = 0;

    memory::Allocator* allocator = nullptr;
    u64 fenceSignals = 0;
    mutable std::mutex mutex;
    std::condition_variable condition;
};

// Slot resolution. Every one returns null for a stale or default handle.
[[nodiscard]] D3D12FenceSlot* Resolve(D3D12Device& device, FenceHandle handle) noexcept;
[[nodiscard]] D3D12BufferSlot* Resolve(D3D12Device& device, BufferHandle handle) noexcept;
[[nodiscard]] D3D12TextureSlot* Resolve(D3D12Device& device, TextureHandle handle) noexcept;
[[nodiscard]] D3D12ViewSlot* Resolve(D3D12Device& device, TextureViewHandle handle) noexcept;
[[nodiscard]] D3D12SamplerSlot* Resolve(D3D12Device& device, SamplerHandle handle) noexcept;

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

}  // namespace cyber::rhi::detail

#endif
