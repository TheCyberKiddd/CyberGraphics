#include "d3d12_internal.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include <cstring>

namespace cyber::rhi::detail
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
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

}  // namespace

// ---------------------------------------------------------------------------
// Slot resolution
// ---------------------------------------------------------------------------

D3D12FenceSlot* Resolve(D3D12Device& device, FenceHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.fences.Size())
    {
        return nullptr;
    }
    D3D12FenceSlot& slot = device.fences[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

D3D12BufferSlot* Resolve(D3D12Device& device, BufferHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.buffers.Size())
    {
        return nullptr;
    }
    D3D12BufferSlot& slot = device.buffers[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

D3D12TextureSlot* Resolve(D3D12Device& device, TextureHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.textures.Size())
    {
        return nullptr;
    }
    D3D12TextureSlot& slot = device.textures[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

D3D12ViewSlot* Resolve(D3D12Device& device, TextureViewHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.views.Size())
    {
        return nullptr;
    }
    D3D12ViewSlot& slot = device.views[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

D3D12SamplerSlot* Resolve(D3D12Device& device, SamplerHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.samplers.Size())
    {
        return nullptr;
    }
    D3D12SamplerSlot& slot = device.samplers[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

// ---------------------------------------------------------------------------
// Vocabulary
// ---------------------------------------------------------------------------

DXGI_FORMAT ToDxgiFormat(Format format) noexcept
{
    switch (format)
    {
    case Format::R8Unorm:
        return DXGI_FORMAT_R8_UNORM;
    case Format::RG8Unorm:
        return DXGI_FORMAT_R8G8_UNORM;
    case Format::RGBA8Unorm:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8UnormSrgb:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::BGRA8Unorm:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8UnormSrgb:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case Format::R16Float:
        return DXGI_FORMAT_R16_FLOAT;
    case Format::RG16Float:
        return DXGI_FORMAT_R16G16_FLOAT;
    case Format::RGBA16Float:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::R32Float:
        return DXGI_FORMAT_R32_FLOAT;
    case Format::RG32Float:
        return DXGI_FORMAT_R32G32_FLOAT;
    case Format::RGBA32Float:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Format::R32Uint:
        return DXGI_FORMAT_R32_UINT;
    case Format::RG32Uint:
        return DXGI_FORMAT_R32G32_UINT;
    case Format::RGBA32Uint:
        return DXGI_FORMAT_R32G32B32A32_UINT;
    case Format::R10G10B10A2Unorm:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case Format::RG11B10Float:
        return DXGI_FORMAT_R11G11B10_FLOAT;
    case Format::D16Unorm:
        return DXGI_FORMAT_D16_UNORM;
    case Format::D32Float:
        return DXGI_FORMAT_D32_FLOAT;
    case Format::D24UnormS8Uint:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32FloatS8Uint:
        return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case Format::BC1Unorm:
        return DXGI_FORMAT_BC1_UNORM;
    case Format::BC1UnormSrgb:
        return DXGI_FORMAT_BC1_UNORM_SRGB;
    case Format::BC3Unorm:
        return DXGI_FORMAT_BC3_UNORM;
    case Format::BC3UnormSrgb:
        return DXGI_FORMAT_BC3_UNORM_SRGB;
    case Format::BC5Unorm:
        return DXGI_FORMAT_BC5_UNORM;
    case Format::BC7Unorm:
        return DXGI_FORMAT_BC7_UNORM;
    case Format::BC7UnormSrgb:
        return DXGI_FORMAT_BC7_UNORM_SRGB;
    case Format::R32G32B32Float:
        // No DXGI format: the API reaches three-component vectors through a
        // vertex layout offset, not through a texel format.
        return DXGI_FORMAT_UNKNOWN;
    case Format::Count:
        break;
    }
    return DXGI_FORMAT_UNKNOWN;
}

D3D12_RESOURCE_STATES ToD3D12State(ResourceState state) noexcept
{
    // A read state is a combination of bits, and D3D12 accepts exactly that:
    // SHADER_RESOURCE for a sampled read, for instance. The switch handles the
    // single states first and the combinations after.
    if (state == ResourceState::Undefined)
    {
        return D3D12_RESOURCE_STATE_COMMON;
    }
    if (state == ResourceState::Present)
    {
        return D3D12_RESOURCE_STATE_PRESENT;
    }
    if (state == ResourceState::CopySource)
    {
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    if (state == ResourceState::CopyDestination)
    {
        return D3D12_RESOURCE_STATE_COPY_DEST;
    }
    if (state == ResourceState::VertexBuffer)
    {
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    }
    if (state == ResourceState::IndexBuffer)
    {
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    }
    if (state == ResourceState::ConstantBuffer)
    {
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    }
    if (state == ResourceState::ShaderRead)
    {
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    if (state == ResourceState::ShaderWrite)
    {
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (state == ResourceState::RenderTarget)
    {
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    if (state == ResourceState::DepthRead)
    {
        return D3D12_RESOURCE_STATE_DEPTH_READ;
    }
    if (state == ResourceState::DepthWrite)
    {
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    if (state == ResourceState::IndirectArgument)
    {
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    // A combination. Build it bit by bit so a caller that ORs read states gets
    // the union the API expects.
    D3D12_RESOURCE_STATES native = D3D12_RESOURCE_STATE_COMMON;
    const u32 bits = static_cast<u32>(state);
    const struct
    {
        ResourceState rhi;
        D3D12_RESOURCE_STATES dx;
    } table[] = {
        {ResourceState::CopySource, D3D12_RESOURCE_STATE_COPY_SOURCE},
        {ResourceState::CopyDestination, D3D12_RESOURCE_STATE_COPY_DEST},
        {ResourceState::VertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER},
        {ResourceState::IndexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER},
        {ResourceState::ConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER},
        {ResourceState::ShaderRead, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        {ResourceState::ShaderWrite, D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
        {ResourceState::RenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET},
        {ResourceState::DepthRead, D3D12_RESOURCE_STATE_DEPTH_READ},
        {ResourceState::DepthWrite, D3D12_RESOURCE_STATE_DEPTH_WRITE},
        {ResourceState::IndirectArgument, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT},
    };
    for (const auto& entry : table)
    {
        if ((bits & static_cast<u32>(entry.rhi)) != 0)
        {
            native |= entry.dx;
        }
    }
    return native == D3D12_RESOURCE_STATE_COMMON ? D3D12_RESOURCE_STATE_COMMON : native;
}

ResourceState FromD3D12State(D3D12_RESOURCE_STATES state) noexcept
{
    // `D3D12_RESOURCE_STATE_COMMON` and `D3D12_RESOURCE_STATE_PRESENT` are both
    // zero, so they cannot be told apart here. Common is the state a committed
    // resource starts in and what this backend assumes, so the shared value reads
    // as `Undefined`; a swapchain image's present state is tracked by the
    // swapchain itself once it lands.
    if (state == 0)
    {
        return ResourceState::Undefined;
    }
    switch (state)
    {
    case D3D12_RESOURCE_STATE_COPY_SOURCE:
        return ResourceState::CopySource;
    case D3D12_RESOURCE_STATE_COPY_DEST:
        return ResourceState::CopyDestination;
    case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER:
        return ResourceState::VertexBuffer;
    case D3D12_RESOURCE_STATE_INDEX_BUFFER:
        return ResourceState::IndexBuffer;
    case D3D12_RESOURCE_STATE_RENDER_TARGET:
        return ResourceState::RenderTarget;
    case D3D12_RESOURCE_STATE_DEPTH_WRITE:
        return ResourceState::DepthWrite;
    case D3D12_RESOURCE_STATE_DEPTH_READ:
        return ResourceState::DepthRead;
    case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
        return ResourceState::ShaderWrite;
    case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT:
        return ResourceState::IndirectArgument;
    case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
    case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
        return ResourceState::ShaderRead;
    default:
        return ResourceState::Undefined;
    }
}

D3D12_RESOURCE_DIMENSION ToD3D12Dimension(TextureDimension dimension) noexcept
{
    switch (dimension)
    {
    case TextureDimension::Dim1D:
        return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case TextureDimension::Dim3D:
        return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    case TextureDimension::Dim2D:
    case TextureDimension::Cube:
    case TextureDimension::Count:
        break;
    }
    // A cube is a 2D array in every API, so it takes the 2D dimension.
    return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
}

D3D12_COMPARISON_FUNC ToComparisonFunc(CompareFunction function) noexcept
{
    switch (function)
    {
    case CompareFunction::Never:
        return D3D12_COMPARISON_FUNC_NEVER;
    case CompareFunction::Less:
        return D3D12_COMPARISON_FUNC_LESS;
    case CompareFunction::Equal:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareFunction::LessEqual:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareFunction::Greater:
        return D3D12_COMPARISON_FUNC_GREATER;
    case CompareFunction::NotEqual:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareFunction::GreaterEqual:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareFunction::Always:
    case CompareFunction::Count:
        break;
    }
    return D3D12_COMPARISON_FUNC_ALWAYS;
}

D3D12_FILTER ToFilter(const SamplerDesc& desc) noexcept
{
    const bool minLinear = desc.minFilter == FilterMode::Linear;
    const bool magLinear = desc.magFilter == FilterMode::Linear;
    const bool mipLinear = desc.mipmapFilter == FilterMode::Linear;
    const bool compare = desc.compareEnabled;
    const bool anisotropic = desc.maxAnisotropy > 1;

    if (anisotropic)
    {
        return compare ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
    }

    // The API names filters by which stage is linear, so each of the eight
    // combinations has its own enumerator.
    const u32 code = (minLinear ? 0x1U : 0U) | (magLinear ? 0x2U : 0U) | (mipLinear ? 0x4U : 0U);
    const D3D12_FILTER linear[8] = {
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT,
        D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
        D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
        D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
        D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
    };
    const D3D12_FILTER comparison[8] = {
        D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
        D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT,
        D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT,
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR,
        D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
        D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR,
        D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
    };
    return compare ? comparison[code] : linear[code];
}

D3D12_TEXTURE_ADDRESS_MODE ToAddressMode(AddressMode mode) noexcept
{
    switch (mode)
    {
    case AddressMode::Repeat:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case AddressMode::MirroredRepeat:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case AddressMode::ClampToEdge:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case AddressMode::ClampToBorder:
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case AddressMode::Count:
        break;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

D3D12_HEAP_TYPE ToHeapType(BufferUsage usage) noexcept
{
    if (HasUsage(usage, BufferUsage::HostVisible))
    {
        return D3D12_HEAP_TYPE_UPLOAD;
    }
    if (HasUsage(usage, BufferUsage::Readback))
    {
        return D3D12_HEAP_TYPE_READBACK;
    }
    return D3D12_HEAP_TYPE_DEFAULT;
}

D3D12_RESOURCE_FLAGS ToTextureFlags(TextureUsage usage) noexcept
{
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (HasUsage(usage, TextureUsage::RenderTarget))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (HasUsage(usage, TextureUsage::DepthStencil))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (HasUsage(usage, TextureUsage::Storage))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    return flags;
}

u32 SubresourceCount(const TextureDesc& desc) noexcept
{
    return desc.mipLevels * desc.arrayLayers;
}

u32 SubresourceIndex(const TextureDesc& desc, u32 mipLevel, u32 arrayLayer) noexcept
{
    return mipLevel + arrayLayer * desc.mipLevels;
}

void NameObject(ID3D12Object* object, const char* label) noexcept
{
    if (object == nullptr || label == nullptr)
    {
        return;
    }
    // A fixed buffer is enough for a debug name and keeps this off the heap. The
    // conversion is UTF-8 to UTF-16 because that is what the API stores.
    wchar_t wide[128]{};
    const int length = MultiByteToWideChar(CP_UTF8, 0, label, -1, wide, 128);
    if (length > 0)
    {
        (void)object->SetName(wide);
    }
}

Status<Error> SubmitBarriers(D3D12Device& device, const D3D12_RESOURCE_BARRIER* barriers,
                             u32 count) noexcept
{
    if (count == 0)
    {
        return Status<Error>::Ok();
    }
    D3D12Queue& graphics = device.queues[static_cast<u32>(QueueKind::Graphics)];
    if (graphics.queue == nullptr)
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: no graphics queue for barriers");
    }

    // A transition outside a recording needs its own list, allocator, and fence:
    // the API has no immediate-mode entry point.
    ID3D12CommandAllocator* allocator = nullptr;
    if (FAILED(device.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     __uuidof(ID3D12CommandAllocator),
                                                     reinterpret_cast<void**>(&allocator))) ||
        allocator == nullptr)
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: barrier allocator creation failed");
    }
    ID3D12GraphicsCommandList* list = nullptr;
    if (FAILED(device.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                                nullptr, __uuidof(ID3D12GraphicsCommandList),
                                                reinterpret_cast<void**>(&list))) ||
        list == nullptr)
    {
        allocator->Release();
        return StatusError(ErrorCode::Internal, "d3d12 backend: barrier list creation failed");
    }

    list->ResourceBarrier(count, barriers);
    Status<Error> result = Status<Error>::Ok();
    if (FAILED(list->Close()))
    {
        result = StatusError(ErrorCode::Internal, "d3d12 backend: barrier list close failed");
    }
    else
    {
        ID3D12CommandList* lists[] = {list};
        graphics.queue->ExecuteCommandLists(1, lists);
        ++graphics.submissions;

        ID3D12Fence* fence = nullptr;
        if (SUCCEEDED(device.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                                 reinterpret_cast<void**>(&fence))) &&
            fence != nullptr)
        {
            const u64 value = ++graphics.signalled;
            if (SUCCEEDED(graphics.queue->Signal(fence, value)))
            {
                HANDLE event =
                    CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
                if (event != nullptr)
                {
                    (void)fence->SetEventOnCompletion(value, event);
                    (void)WaitForSingleObject(event, INFINITE);
                    (void)CloseHandle(event);
                }
            }
            fence->Release();
        }
        else
        {
            result = StatusError(ErrorCode::Internal, "d3d12 backend: barrier fence failed");
        }
    }
    list->Release();
    allocator->Release();
    return result;
}

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateBuffer(void* internal, const BufferDesc& desc, BufferHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: buffer creation needs a device and an output handle");
    }
    if (desc.sizeBytes == 0)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: buffer size is zero");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (desc.sizeBytes > device.capabilities.limits.maxBufferBytes)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: buffer size is above Limits::maxBufferBytes");
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = ToHeapType(desc.usage);
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Alignment = 0;
    resource.Width = desc.sizeBytes;
    resource.Height = 1;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.Format = DXGI_FORMAT_UNKNOWN;
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource.Flags = HasUsage(desc.usage, BufferUsage::Storage)
                         ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                         : D3D12_RESOURCE_FLAG_NONE;

    // The API fixes the initial state per heap type: an upload heap must start in
    // GENERIC_READ and a readback heap in COPY_DEST.
    D3D12_RESOURCE_STATES initial = D3D12_RESOURCE_STATE_COMMON;
    if (heap.Type == D3D12_HEAP_TYPE_UPLOAD)
    {
        initial = D3D12_RESOURCE_STATE_GENERIC_READ;
    }
    else if (heap.Type == D3D12_HEAP_TYPE_READBACK)
    {
        initial = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    ID3D12Resource* resource3D = nullptr;
    if (FAILED(device.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource,
                                                      initial, nullptr, __uuidof(ID3D12Resource),
                                                      reinterpret_cast<void**>(&resource3D))) ||
        resource3D == nullptr)
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: buffer creation failed");
    }
    NameObject(resource3D, desc.label);

    void* mapped = nullptr;
    if (initial == D3D12_RESOURCE_STATE_GENERIC_READ || heap.Type == D3D12_HEAP_TYPE_READBACK)
    {
        // A host heap stays mapped for its lifetime: mapping per write costs more
        // than the write on a write-combined heap.
        const D3D12_RANGE range{0, 0};
        if (FAILED(resource3D->Map(0, &range, &mapped)))
        {
            resource3D->Release();
            return StatusError(ErrorCode::Internal, "d3d12 backend: buffer map failed");
        }
    }

    u32 index = kInvalidHandleIndex;
    if (device.freeBufferSlots.Size() > 0)
    {
        index = device.freeBufferSlots[device.freeBufferSlots.Size() - 1];
        (void)device.freeBufferSlots.TryResize(device.freeBufferSlots.Size() - 1);
    }
    else
    {
        if (!device.buffers.TryPushBack(D3D12BufferSlot{}))
        {
            if (mapped != nullptr)
            {
                resource3D->Unmap(0, nullptr);
            }
            resource3D->Release();
            return StatusError(ErrorCode::Internal, "d3d12 backend: buffer table growth failed");
        }
        index = static_cast<u32>(device.buffers.Size() - 1);
    }

    D3D12BufferSlot& slot = device.buffers[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.resource = resource3D;
    slot.mapped = mapped;
    slot.desc = desc;
    slot.state = FromD3D12State(initial);
    slot.native = initial;

    BufferHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyBuffer(void* internal, BufferHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12BufferSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: buffer handle is stale or invalid");
    }
    if (slot->mapped != nullptr)
    {
        slot->resource->Unmap(0, nullptr);
        slot->mapped = nullptr;
    }
    slot->resource->Release();
    slot->resource = nullptr;
    slot->alive = false;
    slot->generation = NextGeneration(slot->generation);
    slot->state = ResourceState::Undefined;
    if (!device.freeBufferSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: buffer free list growth failed");
    }
    return Status<Error>::Ok();
}

BufferInfo D3D12BufferInfo(const void* internal, BufferHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return BufferInfo{};
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12BufferSlot* slot = Resolve(const_cast<D3D12Device&>(device), handle);
    if (slot == nullptr)
    {
        return BufferInfo{};
    }
    BufferInfo info{};
    info.valid = true;
    info.sizeBytes = slot->desc.sizeBytes;
    info.elementStride = slot->desc.elementStride;
    info.usage = slot->desc.usage;
    return info;
}

Status<Error> D3D12WriteBuffer(void* internal, BufferHandle handle, u64 offset,
                               cyber::memory::Span<const cyber::memory::Byte> bytes) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12BufferSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: buffer handle is stale or invalid");
    }
    if (!HasUsage(slot->desc.usage, BufferUsage::HostVisible) || slot->mapped == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: buffer is not HostVisible, so it cannot be written");
    }
    if (offset > slot->desc.sizeBytes || bytes.Size() > slot->desc.sizeBytes - offset)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: write range is outside the buffer");
    }
    if (bytes.Size() > 0)
    {
        std::memcpy(static_cast<cyber::core::u8*>(slot->mapped) + offset, bytes.Data(),
                    bytes.Size());
    }
    return Status<Error>::Ok();
}

Status<Error> D3D12ReadBuffer(void* internal, BufferHandle handle, u64 offset,
                              cyber::memory::Span<cyber::memory::Byte> bytes) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12BufferSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: buffer handle is stale or invalid");
    }
    if (!HasUsage(slot->desc.usage, BufferUsage::Readback) || slot->mapped == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: buffer is not Readback, so it cannot be read");
    }
    if (offset > slot->desc.sizeBytes || bytes.Size() > slot->desc.sizeBytes - offset)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: read range is outside the buffer");
    }
    if (bytes.Size() > 0)
    {
        std::memcpy(bytes.Data(), static_cast<const cyber::core::u8*>(slot->mapped) + offset,
                    bytes.Size());
    }
    return Status<Error>::Ok();
}

ResourceState D3D12BufferState(const void* internal, BufferHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return ResourceState::Undefined;
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12BufferSlot* slot = Resolve(const_cast<D3D12Device&>(device), handle);
    return slot != nullptr ? slot->state : ResourceState::Undefined;
}

}  // namespace cyber::rhi::detail

#endif
