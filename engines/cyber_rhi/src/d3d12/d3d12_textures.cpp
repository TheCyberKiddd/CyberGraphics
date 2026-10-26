#include "d3d12_internal.h"

#if defined(CYBER_RHI_HAS_D3D12)

namespace cyber::rhi::detail
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::kInvalidHandleIndex;

[[nodiscard]] Status<Error> StatusError(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

[[nodiscard]] u32 NextGeneration(u32 current) noexcept
{
    const u32 next = current + 1;
    return next == 0 ? 1 : next;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE ViewCpuHandle(D3D12Device& device, u32 index) noexcept
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = device.viewHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * device.viewDescriptorSize;
    return handle;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE SamplerCpuHandle(D3D12Device& device, u32 index) noexcept
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = device.samplerHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * device.samplerDescriptorSize;
    return handle;
}

// Border colour as the four floats the API stores.
void BorderColorValues(BorderColor color, float (&out)[4]) noexcept
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 0.0f;
    switch (color)
    {
    case BorderColor::TransparentBlack:
        break;
    case BorderColor::OpaqueBlack:
        out[3] = 1.0f;
        break;
    case BorderColor::OpaqueWhite:
        out[0] = 1.0f;
        out[1] = 1.0f;
        out[2] = 1.0f;
        out[3] = 1.0f;
        break;
    case BorderColor::Count:
        break;
    }
}

// How many mip levels and layers a view addresses, with zero meaning "the rest".
[[nodiscard]] u32 ResolveMipCount(const TextureDesc& texture,
                                  const SubresourceRange& range) noexcept
{
    const u32 remaining =
        texture.mipLevels > range.baseMipLevel ? texture.mipLevels - range.baseMipLevel : 0;
    if (range.mipLevelCount == 0)
    {
        return remaining;
    }
    return range.mipLevelCount < remaining ? range.mipLevelCount : remaining;
}

[[nodiscard]] u32 ResolveLayerCount(const TextureDesc& texture,
                                    const SubresourceRange& range) noexcept
{
    const u32 remaining =
        texture.arrayLayers > range.baseArrayLayer ? texture.arrayLayers - range.baseArrayLayer : 0;
    if (range.layerCount == 0)
    {
        return remaining;
    }
    return range.layerCount < remaining ? range.layerCount : remaining;
}

}  // namespace

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateTexture(void* internal, const TextureDesc& desc,
                                 TextureHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture creation needs a device and an output handle");
    }

    const DXGI_FORMAT format = ToDxgiFormat(desc.format);
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        return StatusError(ErrorCode::Unsupported, "d3d12 backend: format has no DXGI equivalent");
    }
    if (desc.mipLevels == 0 || desc.mipLevels > 15)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: mip level count is outside 1..15");
    }
    if (SubresourceCount(desc) > kMaxTrackedSubresources)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: texture has more subresources than this backend tracks");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = ToD3D12Dimension(desc.dimension);
    resource.Alignment = 0;
    resource.Width = desc.width;
    resource.Height = desc.dimension == TextureDimension::Dim1D ? 1 : desc.height;
    resource.DepthOrArraySize = static_cast<UINT16>(
        desc.dimension == TextureDimension::Dim3D ? desc.depth : desc.arrayLayers);
    resource.MipLevels = static_cast<UINT16>(desc.mipLevels);
    resource.Format = format;
    resource.SampleDesc.Count = desc.sampleCount == 0 ? 1 : desc.sampleCount;
    resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource.Flags = ToTextureFlags(desc.usage);

    // A depth attachment cannot also be a render target in one resource, and the
    // API rejects the combination rather than ignoring it.
    if (HasUsage(desc.usage, TextureUsage::RenderTarget) &&
        HasUsage(desc.usage, TextureUsage::DepthStencil))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: a texture cannot be a color and a depth attachment");
    }

    D3D12_CLEAR_VALUE clear{};
    const D3D12_CLEAR_VALUE* clearPointer = nullptr;
    if (HasUsage(desc.usage, TextureUsage::RenderTarget))
    {
        clear.Format = format;
        // Reverse-Z: the far plane is 0, which is what a fresh depth buffer wants
        // and what a fresh color buffer clears to for the engine's default.
        clear.Color[3] = 1.0f;
        clearPointer = &clear;
    }
    else if (HasUsage(desc.usage, TextureUsage::DepthStencil))
    {
        clear.Format = format;
        clear.DepthStencil.Depth = 1.0f;
        clear.DepthStencil.Stencil = 0;
        clearPointer = &clear;
    }

    ID3D12Resource* texture = nullptr;
    if (FAILED(device.device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_COMMON, clearPointer,
            __uuidof(ID3D12Resource), reinterpret_cast<void**>(&texture))) ||
        texture == nullptr)
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: texture creation failed");
    }
    NameObject(texture, desc.label);

    u32 index = kInvalidHandleIndex;
    if (device.freeTextureSlots.Size() > 0)
    {
        index = device.freeTextureSlots[device.freeTextureSlots.Size() - 1];
        (void)device.freeTextureSlots.TryResize(device.freeTextureSlots.Size() - 1);
    }
    else
    {
        if (!device.textures.TryPushBack(D3D12TextureSlot{}))
        {
            texture->Release();
            return StatusError(ErrorCode::Internal, "d3d12 backend: texture table growth failed");
        }
        index = static_cast<u32>(device.textures.Size() - 1);
    }

    D3D12TextureSlot& slot = device.textures[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.resource = texture;
    slot.desc = desc;
    for (u32 subresource = 0; subresource < kMaxTrackedSubresources; ++subresource)
    {
        slot.states[subresource] = ResourceState::Undefined;
        slot.native[subresource] = D3D12_RESOURCE_STATE_COMMON;
    }

    TextureHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyTexture(void* internal, TextureHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12TextureSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture handle is stale or invalid");
    }

    // Views of a dying texture are retired with it, so a view handle can never
    // describe a released resource.
    for (u32 index = 0; index < device.views.Size(); ++index)
    {
        D3D12ViewSlot& view = device.views[index];
        if (view.alive && view.texture == handle)
        {
            view.alive = false;
            view.generation = NextGeneration(view.generation);
            view.texture = TextureHandle{};
            (void)device.freeViewSlots.TryPushBack(index);
        }
    }

    slot->resource->Release();
    slot->resource = nullptr;
    slot->alive = false;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freeTextureSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: texture free list growth failed");
    }
    return Status<Error>::Ok();
}

TextureInfo D3D12TextureInfo(const void* internal, TextureHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return TextureInfo{};
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12TextureSlot* slot = Resolve(const_cast<D3D12Device&>(device), handle);
    if (slot == nullptr)
    {
        return TextureInfo{};
    }
    TextureInfo info{};
    info.valid = true;
    info.dimension = slot->desc.dimension;
    info.format = slot->desc.format;
    info.usage = slot->desc.usage;
    info.width = slot->desc.width;
    info.height = slot->desc.height;
    info.depth = slot->desc.depth;
    info.arrayLayers = slot->desc.arrayLayers;
    info.mipLevels = slot->desc.mipLevels;
    info.sampleCount = slot->desc.sampleCount;
    info.sizeBytes = TextureByteSize(slot->desc);
    return info;
}

ResourceState D3D12TextureState(const void* internal, TextureHandle handle, u32 mipLevel,
                                u32 arrayLayer) noexcept
{
    if (internal == nullptr)
    {
        return ResourceState::Undefined;
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12TextureSlot* slot = Resolve(const_cast<D3D12Device&>(device), handle);
    if (slot == nullptr || mipLevel >= slot->desc.mipLevels || arrayLayer >= slot->desc.arrayLayers)
    {
        return ResourceState::Undefined;
    }
    return slot->states[SubresourceIndex(slot->desc, mipLevel, arrayLayer)];
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateTextureView(void* internal, const TextureViewDesc& desc,
                                     TextureViewHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: view creation needs a device and an output handle");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    D3D12TextureSlot* texture = Resolve(device, desc.texture);
    if (texture == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: view names a stale texture");
    }

    const DXGI_FORMAT format = ToDxgiFormat(desc.format);
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        return StatusError(ErrorCode::Unsupported,
                           "d3d12 backend: view format has no DXGI equivalent");
    }

    const u32 mipCount = ResolveMipCount(texture->desc, desc.range);
    const u32 layerCount = ResolveLayerCount(texture->desc, desc.range);
    if (mipCount == 0 || layerCount == 0)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: view range is outside the texture");
    }

    const u32 descriptorIndex = device.viewDescriptorsUsed;
    const u32 capacity = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;
    if (descriptorIndex >= capacity)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: shader-visible descriptor heap is full");
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE destination = ViewCpuHandle(device, descriptorIndex);

    const bool depthTexture = HasUsage(texture->desc.usage, TextureUsage::DepthStencil);
    if (depthTexture)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = texture->desc.arrayLayers > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY
                                                           : D3D12_DSV_DIMENSION_TEXTURE2D;
        view.Texture2DArray.MipSlice = desc.range.baseMipLevel;
        view.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
        view.Texture2DArray.ArraySize = layerCount;
        device.device->CreateDepthStencilView(texture->resource, &view, destination);
    }
    else if (HasUsage(texture->desc.usage, TextureUsage::RenderTarget))
    {
        D3D12_RENDER_TARGET_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = texture->desc.arrayLayers > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY
                                                           : D3D12_RTV_DIMENSION_TEXTURE2D;
        view.Texture2DArray.MipSlice = desc.range.baseMipLevel;
        view.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
        view.Texture2DArray.ArraySize = layerCount;
        device.device->CreateRenderTargetView(texture->resource, &view, destination);
    }
    else
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = format;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (desc.dimension == TextureDimension::Cube)
        {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            view.TextureCube.MipLevels = mipCount;
            view.TextureCube.MostDetailedMip = desc.range.baseMipLevel;
        }
        else if (desc.dimension == TextureDimension::Dim3D)
        {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            view.Texture3D.MipLevels = mipCount;
            view.Texture3D.MostDetailedMip = desc.range.baseMipLevel;
        }
        else if (desc.dimension == TextureDimension::Dim1D)
        {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            view.Texture1D.MipLevels = mipCount;
            view.Texture1D.MostDetailedMip = desc.range.baseMipLevel;
        }
        else if (layerCount > 1)
        {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.MipLevels = mipCount;
            view.Texture2DArray.MostDetailedMip = desc.range.baseMipLevel;
            view.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
            view.Texture2DArray.ArraySize = layerCount;
        }
        else
        {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = mipCount;
            view.Texture2D.MostDetailedMip = desc.range.baseMipLevel;
        }
        device.device->CreateShaderResourceView(texture->resource, &view, destination);
    }
    device.viewDescriptorsUsed = descriptorIndex + 1;

    u32 index = kInvalidHandleIndex;
    if (device.freeViewSlots.Size() > 0)
    {
        index = device.freeViewSlots[device.freeViewSlots.Size() - 1];
        (void)device.freeViewSlots.TryResize(device.freeViewSlots.Size() - 1);
    }
    else
    {
        if (!device.views.TryPushBack(D3D12ViewSlot{}))
        {
            return StatusError(ErrorCode::Internal, "d3d12 backend: view table growth failed");
        }
        index = static_cast<u32>(device.views.Size() - 1);
    }

    D3D12ViewSlot& slot = device.views[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.texture = desc.texture;
    slot.desc = desc;
    slot.descriptorIndex = descriptorIndex;

    TextureViewHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyTextureView(void* internal, TextureViewHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12ViewSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: view handle is stale or invalid");
    }
    slot->alive = false;
    slot->generation = NextGeneration(slot->generation);
    slot->texture = TextureHandle{};
    if (!device.freeViewSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: view free list growth failed");
    }
    return Status<Error>::Ok();
}

TextureViewInfo D3D12TextureViewInfo(const void* internal, TextureViewHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return TextureViewInfo{};
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12ViewSlot* slot = Resolve(const_cast<D3D12Device&>(device), handle);
    if (slot == nullptr)
    {
        return TextureViewInfo{};
    }
    TextureViewInfo info{};
    info.valid = true;
    info.format = slot->desc.format;
    info.dimension = slot->desc.dimension;
    info.range = slot->desc.range;
    return info;
}

// ---------------------------------------------------------------------------
// Samplers
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateSampler(void* internal, const SamplerDesc& desc,
                                 SamplerHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: sampler creation needs a device and an output handle");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    const u32 descriptorIndex = device.samplerDescriptorsUsed;
    if (descriptorIndex >= D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: shader-visible sampler heap is full");
    }

    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter = ToFilter(desc);
    sampler.AddressU = ToAddressMode(desc.addressU);
    sampler.AddressV = ToAddressMode(desc.addressV);
    sampler.AddressW = ToAddressMode(desc.addressW);
    sampler.MipLODBias = desc.mipLodBias;
    sampler.MaxAnisotropy = desc.maxAnisotropy == 0 ? 1 : desc.maxAnisotropy;
    sampler.ComparisonFunc = ToComparisonFunc(desc.compareFunction);
    BorderColorValues(desc.borderColor, sampler.BorderColor);
    sampler.MinLOD = desc.minLod;
    sampler.MaxLOD = desc.maxLod;
    device.device->CreateSampler(&sampler, SamplerCpuHandle(device, descriptorIndex));
    device.samplerDescriptorsUsed = descriptorIndex + 1;

    u32 index = kInvalidHandleIndex;
    if (device.freeSamplerSlots.Size() > 0)
    {
        index = device.freeSamplerSlots[device.freeSamplerSlots.Size() - 1];
        (void)device.freeSamplerSlots.TryResize(device.freeSamplerSlots.Size() - 1);
    }
    else
    {
        if (!device.samplers.TryPushBack(D3D12SamplerSlot{}))
        {
            return StatusError(ErrorCode::Internal, "d3d12 backend: sampler table growth failed");
        }
        index = static_cast<u32>(device.samplers.Size() - 1);
    }

    D3D12SamplerSlot& slot = device.samplers[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.desc = desc;
    slot.descriptorIndex = descriptorIndex;

    SamplerHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroySampler(void* internal, SamplerHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12SamplerSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: sampler handle is stale or invalid");
    }
    slot->alive = false;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freeSamplerSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: sampler free list growth failed");
    }
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

Status<Error> D3D12Transition(void* internal, cyber::memory::Span<const BufferBarrier> buffers,
                              cyber::memory::Span<const TextureBarrier> textures) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    const u32 total = static_cast<u32>(buffers.Size() + textures.Size());
    if (total == 0)
    {
        return Status<Error>::Ok();
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    // Validate the whole batch before touching any state, so a batch with one bad
    // barrier changes nothing and reports the bad one.
    for (usize index = 0; index < buffers.Size(); ++index)
    {
        const BufferBarrier& barrier = buffers[index];
        const D3D12BufferSlot* slot = Resolve(device, barrier.buffer);
        if (slot == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: barrier names a stale buffer");
        }
        if (slot->mapped != nullptr)
        {
            return StatusError(ErrorCode::Unsupported,
                               "d3d12 backend: a host-visible buffer has a fixed state");
        }
        if (barrier.from != slot->state && barrier.from != ResourceState::Undefined)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: barrier 'from' does not match the buffer's state");
        }
        if (barrier.to == ResourceState::Undefined)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: barrier target is Undefined");
        }
    }
    for (usize index = 0; index < textures.Size(); ++index)
    {
        const TextureBarrier& barrier = textures[index];
        const D3D12TextureSlot* slot = Resolve(device, barrier.texture);
        if (slot == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: barrier names a stale texture");
        }
        if (barrier.range.baseMipLevel >= slot->desc.mipLevels ||
            barrier.range.baseArrayLayer >= slot->desc.arrayLayers)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: barrier range is outside the texture");
        }
        if (barrier.to == ResourceState::Undefined)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: barrier target is Undefined");
        }
    }

    cyber::memory::DynamicArray<D3D12_RESOURCE_BARRIER> barriers(*device.allocator);
    if (!barriers.TryReserve(total))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: barrier list growth failed");
    }

    for (usize index = 0; index < buffers.Size(); ++index)
    {
        const BufferBarrier& barrier = buffers[index];
        D3D12BufferSlot* slot = Resolve(device, barrier.buffer);
        const D3D12_RESOURCE_STATES before =
            barrier.from == ResourceState::Undefined ? slot->native : ToD3D12State(barrier.from);
        const D3D12_RESOURCE_STATES after = ToD3D12State(barrier.to);
        slot->state = barrier.to;
        slot->native = after;

        D3D12_RESOURCE_BARRIER entry{};
        entry.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        entry.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        entry.Transition.pResource = slot->resource;
        entry.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        entry.Transition.StateBefore = before;
        entry.Transition.StateAfter = after;
        (void)barriers.TryPushBack(entry);
    }

    for (usize index = 0; index < textures.Size(); ++index)
    {
        const TextureBarrier& barrier = textures[index];
        D3D12TextureSlot* slot = Resolve(device, barrier.texture);
        const u32 firstMip = barrier.range.baseMipLevel;
        const u32 mipCount = barrier.range.mipLevelCount == 0
                                 ? slot->desc.mipLevels - firstMip
                                 : (barrier.range.mipLevelCount < slot->desc.mipLevels - firstMip
                                        ? barrier.range.mipLevelCount
                                        : slot->desc.mipLevels - firstMip);
        const u32 firstLayer = barrier.range.baseArrayLayer;
        const u32 layerCount = barrier.range.layerCount == 0
                                   ? slot->desc.arrayLayers - firstLayer
                                   : (barrier.range.layerCount < slot->desc.arrayLayers - firstLayer
                                          ? barrier.range.layerCount
                                          : slot->desc.arrayLayers - firstLayer);

        for (u32 layer = firstLayer; layer < firstLayer + layerCount; ++layer)
        {
            for (u32 mip = firstMip; mip < firstMip + mipCount; ++mip)
            {
                const u32 subresource = SubresourceIndex(slot->desc, mip, layer);
                const D3D12_RESOURCE_STATES before = barrier.from == ResourceState::Undefined
                                                         ? slot->native[subresource]
                                                         : ToD3D12State(barrier.from);
                const D3D12_RESOURCE_STATES after = ToD3D12State(barrier.to);
                slot->states[subresource] = barrier.to;
                slot->native[subresource] = after;

                D3D12_RESOURCE_BARRIER entry{};
                entry.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                entry.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                entry.Transition.pResource = slot->resource;
                entry.Transition.Subresource = subresource;
                entry.Transition.StateBefore = before;
                entry.Transition.StateAfter = after;
                (void)barriers.TryPushBack(entry);
            }
        }
    }

    return SubmitBarriers(device, barriers.Data(), static_cast<u32>(barriers.Size()));
}

void ReleaseResources(D3D12Device& device) noexcept
{
    for (u32 index = 0; index < device.pipelineLayouts.Size(); ++index)
    {
        D3D12PipelineLayoutSlot& slot = device.pipelineLayouts[index];
        if (slot.rootSignature != nullptr)
        {
            slot.rootSignature->Release();
            slot.rootSignature = nullptr;
        }
        slot.alive = false;
    }
    for (u32 index = 0; index < device.shaders.Size(); ++index)
    {
        D3D12ShaderSlot& slot = device.shaders[index];
        if (slot.code != nullptr && device.allocator != nullptr)
        {
            device.allocator->Deallocate(slot.code);
            slot.code = nullptr;
        }
        slot.alive = false;
    }
    for (u32 index = 0; index < device.groups.Size(); ++index)
    {
        device.groups[index].alive = false;
    }
    for (u32 index = 0; index < device.layouts.Size(); ++index)
    {
        device.layouts[index].alive = false;
    }
    for (u32 index = 0; index < device.views.Size(); ++index)
    {
        D3D12ViewSlot& slot = device.views[index];
        slot.alive = false;
        slot.texture = TextureHandle{};
    }
    for (u32 index = 0; index < device.samplers.Size(); ++index)
    {
        device.samplers[index].alive = false;
    }
    for (u32 index = 0; index < device.textures.Size(); ++index)
    {
        D3D12TextureSlot& slot = device.textures[index];
        if (slot.resource != nullptr)
        {
            slot.resource->Release();
            slot.resource = nullptr;
        }
        slot.alive = false;
    }
    for (u32 index = 0; index < device.buffers.Size(); ++index)
    {
        D3D12BufferSlot& slot = device.buffers[index];
        if (slot.mapped != nullptr)
        {
            slot.resource->Unmap(0, nullptr);
            slot.mapped = nullptr;
        }
        if (slot.resource != nullptr)
        {
            slot.resource->Release();
            slot.resource = nullptr;
        }
        slot.alive = false;
    }
}

}  // namespace cyber::rhi::detail

#endif
