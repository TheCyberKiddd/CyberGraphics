#include "d3d12_internal.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include <cyber/core/log.h>
#include <cyber/memory/alignment.h>

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

D3D12TimestampQueryHeapSlot* Resolve(D3D12Device& device, TimestampQueryHeapHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.timestampQueryHeaps.Size())
    {
        return nullptr;
    }
    D3D12TimestampQueryHeapSlot& slot = device.timestampQueryHeaps[handle.index];
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

// The memory class a buffer's usage calls for (ADR 0018). The three classes
// map to the three heap types D3D12 names for host and device placement.
[[nodiscard]] d3d12::MemoryClassType MemoryClassOf(BufferUsage usage) noexcept
{
    if (HasUsage(usage, BufferUsage::Readback))
    {
        return d3d12::MemoryClassType::Readback;
    }
    if (HasUsage(usage, BufferUsage::HostVisible))
    {
        return d3d12::MemoryClassType::Upload;
    }
    return d3d12::MemoryClassType::DeviceLocal;
}

// The heap type a buffer's memory class is placed into. The old per-usage
// version is gone: the class is the one decision the allocator and the resource
// state now share, so a buffer's usage and its heap cannot disagree.
D3D12_HEAP_TYPE ToHeapType(BufferUsage usage) noexcept
{
    switch (MemoryClassOf(usage))
    {
    case d3d12::MemoryClassType::Upload:
        return D3D12_HEAP_TYPE_UPLOAD;
    case d3d12::MemoryClassType::Readback:
        return D3D12_HEAP_TYPE_READBACK;
    case d3d12::MemoryClassType::DeviceLocal:
    case d3d12::MemoryClassType::Count:
        break;
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

// The private-data slot the API's own `SetName` writes. Reading it back is what
// proves the driver has the name, so `SetName`'s equivalence with
// `SetPrivateData` under this well-known identifier is the whole point.
constexpr GUID kD3DDebugObjectNamePrivateData = {
    0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};

// Copies a UTF-8 string into an `ObjectNameInfo`'s fixed buffer, truncating at
// the buffer, and marks the info named. Same shape as the null backend's.
void FillNameFromUtf8(ObjectNameInfo& info, const char* utf8) noexcept
{
    u32 length = 0;
    while (utf8[length] != '\0' && length < sizeof(info.name) - 1)
    {
        info.name[length] = utf8[length];
        ++length;
    }
    info.name[length] = '\0';
    info.length = length;
    info.named = true;
}

// Reads one D3D12 object's debug name back through `GetPrivateData`, which is
// the read-back side of what `NameObject` wrote. A size of zero means the
// object never got a name, which is one more way an unnamed object and a kind
// without a nameable object answer the same.
void ReadNameFromObject(ID3D12Object* object, ObjectNameInfo& info) noexcept
{
    if (object == nullptr)
    {
        return;
    }
    wchar_t wide[128]{};
    u32 wideBytes = sizeof(wide);
    if (FAILED(object->GetPrivateData(kD3DDebugObjectNamePrivateData, &wideBytes, wide)) ||
        wideBytes < sizeof(wchar_t))
    {
        return;
    }
    // The stored length includes both terminator bytes; reduce to the character
    // count `MultiByteToWideChar` understands.
    wide[wideBytes / sizeof(wchar_t) - 1] = L'\0';
    char utf8[256]{};
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (length <= 0)
    {
        return;
    }
    FillNameFromUtf8(info, utf8);
}

[[nodiscard]] ObjectNameInfo D3D12DeviceObjectName(const void* internal,
                                                   const ObjectRef& object) noexcept
{
    ObjectNameInfo info;
    if (internal == nullptr)
    {
        return info;
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    // A kind whose D3D12 object the backend does not own a pointer to (a view,
    // a sampler, a bind group, a shader module, a pipeline cache with no
    // library) keeps no name driver-side, so no case reaches `ReadNameFromObject`
    // and the answer stays unnamed. That is the honest answer, not a workaround.
    switch (object.kind)
    {
    case ObjectKind::Buffer:
    {
        const D3D12BufferSlot* slot =
            Resolve(const_cast<D3D12Device&>(device), UnpackHandle<BufferTag>(object.packed));
        if (slot != nullptr && slot->alive)
        {
            ReadNameFromObject(slot->resource, info);
        }
        break;
    }
    case ObjectKind::Texture:
    {
        const D3D12TextureSlot* slot =
            Resolve(const_cast<D3D12Device&>(device), UnpackHandle<TextureTag>(object.packed));
        if (slot != nullptr && slot->alive)
        {
            ReadNameFromObject(slot->resource, info);
        }
        break;
    }
    case ObjectKind::PipelineLayout:
    {
        const D3D12PipelineLayoutSlot* slot = Resolve(
            const_cast<D3D12Device&>(device), UnpackHandle<PipelineLayoutTag>(object.packed));
        if (slot != nullptr && slot->alive)
        {
            ReadNameFromObject(slot->rootSignature, info);
        }
        break;
    }
    case ObjectKind::Pipeline:
    {
        const D3D12PipelineSlot* slot =
            Resolve(const_cast<D3D12Device&>(device), UnpackHandle<PipelineTag>(object.packed));
        if (slot != nullptr && slot->alive)
        {
            ReadNameFromObject(slot->state, info);
        }
        break;
    }
    case ObjectKind::Fence:
    {
        const D3D12FenceSlot* slot =
            Resolve(const_cast<D3D12Device&>(device), UnpackHandle<FenceTag>(object.packed));
        if (slot != nullptr && slot->alive)
        {
            ReadNameFromObject(slot->fence, info);
        }
        break;
    }
    case ObjectKind::TimestampQueryHeap:
    {
        const D3D12TimestampQueryHeapSlot* slot = Resolve(
            const_cast<D3D12Device&>(device), UnpackHandle<TimestampQueryHeapTag>(object.packed));
        if (slot != nullptr && slot->alive)
        {
            ReadNameFromObject(slot->queries, info);
        }
        break;
    }
    case ObjectKind::CommandList:
    {
        const D3D12CommandListSlot* slot =
            Resolve(const_cast<D3D12Device&>(device), UnpackHandle<CommandListTag>(object.packed));
        if (slot != nullptr && slot->alive)
        {
            ReadNameFromObject(slot->list, info);
        }
        break;
    }
    default:
        break;
    }
    return info;
}

Status<Error> D3D12CreateTimestampQueryHeap(void* internal, const TimestampQueryHeapDesc& desc,
                                            TimestampQueryHeapHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr || desc.queryCount == 0)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: timestamp heap needs a device, count, and output");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const Status<Error> healthy = DeviceLostStatus(device);
    if (!healthy)
    {
        return healthy;
    }
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (!device.capabilities.features.Has(Feature::TimestampQueries))
    {
        return StatusError(ErrorCode::Unsupported,
                           "d3d12 backend: timestamp queries are not supported");
    }

    const u64 readbackBytes = static_cast<u64>(desc.queryCount) * sizeof(u64);
    const u64 reservedBytes =
        cyber::memory::AlignUp(readbackBytes, d3d12::kDefaultPlacementAlignment);
    if (readbackBytes > device.capabilities.limits.maxBufferBytes || reservedBytes == 0)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: timestamp heap exceeds the readback limit");
    }

    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Count = desc.queryCount;
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.NodeMask = 0;
    ID3D12QueryHeap* queries = nullptr;
    if (FAILED(device.device->CreateQueryHeap(&queryDesc, __uuidof(ID3D12QueryHeap),
                                              reinterpret_cast<void**>(&queries))) ||
        queries == nullptr)
    {
        return StatusError(ErrorCode::Internal,
                           "d3d12 backend: timestamp query heap creation failed");
    }
    NameObject(queries, desc.label);

    ID3D12Heap* heap = nullptr;
    u64 heapOffset = 0;
    bool dedicated = false;
    const Status<Error> allocated =
        device.memory.Allocate(d3d12::MemoryClassType::Readback, readbackBytes,
                               d3d12::kDefaultPlacementAlignment, &heap, &heapOffset, &dedicated);
    if (!allocated)
    {
        queries->Release();
        return allocated;
    }

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = readbackBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* readback = nullptr;
    if (FAILED(device.device->CreatePlacedResource(
            heap, heapOffset, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            __uuidof(ID3D12Resource), reinterpret_cast<void**>(&readback))) ||
        readback == nullptr)
    {
        device.memory.Deallocate(d3d12::MemoryClassType::Readback, heap, heapOffset, reservedBytes,
                                 dedicated);
        queries->Release();
        return StatusError(ErrorCode::Internal,
                           "d3d12 backend: timestamp readback resource creation failed");
    }

    void* mapped = nullptr;
    const D3D12_RANGE readRange{0, static_cast<SIZE_T>(readbackBytes)};
    if (FAILED(readback->Map(0, &readRange, &mapped)) || mapped == nullptr)
    {
        readback->Release();
        device.memory.Deallocate(d3d12::MemoryClassType::Readback, heap, heapOffset, reservedBytes,
                                 dedicated);
        queries->Release();
        return StatusError(ErrorCode::Internal, "d3d12 backend: timestamp readback mapping failed");
    }

    u32 index = kInvalidHandleIndex;
    if (device.freeTimestampQueryHeaps.Size() > 0)
    {
        index = device.freeTimestampQueryHeaps[device.freeTimestampQueryHeaps.Size() - 1];
        (void)device.freeTimestampQueryHeaps.TryResize(device.freeTimestampQueryHeaps.Size() - 1);
    }
    else
    {
        if (!device.timestampQueryHeaps.TryPushBack(D3D12TimestampQueryHeapSlot{}))
        {
            readback->Unmap(0, nullptr);
            readback->Release();
            device.memory.Deallocate(d3d12::MemoryClassType::Readback, heap, heapOffset,
                                     reservedBytes, dedicated);
            queries->Release();
            return StatusError(ErrorCode::Internal,
                               "d3d12 backend: timestamp heap table growth failed");
        }
        index = static_cast<u32>(device.timestampQueryHeaps.Size() - 1);
    }

    D3D12TimestampQueryHeapSlot& slot = device.timestampQueryHeaps[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.queries = queries;
    slot.readback = readback;
    slot.mapped = mapped;
    slot.desc = desc;
    slot.readbackBytes = readbackBytes;
    slot.heap = heap;
    slot.heapOffset = heapOffset;
    slot.reservedBytes = reservedBytes;
    slot.dedicated = dedicated;

    TimestampQueryHeapHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyTimestampQueryHeap(void* internal,
                                             TimestampQueryHeapHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12TimestampQueryHeapSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: timestamp heap handle is stale or invalid");
    }
    const D3D12_RANGE written{0, 0};
    slot->readback->Unmap(0, &written);
    slot->readback->Release();
    slot->queries->Release();
    device.memory.Deallocate(d3d12::MemoryClassType::Readback, slot->heap, slot->heapOffset,
                             slot->reservedBytes, slot->dedicated);
    slot->queries = nullptr;
    slot->readback = nullptr;
    slot->mapped = nullptr;
    slot->alive = false;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freeTimestampQueryHeaps.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal,
                           "d3d12 backend: timestamp heap free list growth failed");
    }
    return Status<Error>::Ok();
}

Status<Error> D3D12ReadTimestamps(void* internal, TimestampQueryHeapHandle handle, u32 firstQuery,
                                  u32 queryCount, FenceHandle completionFence, u64 completionValue,
                                  memory::Span<u64> out) noexcept
{
    if (internal == nullptr || queryCount == 0 || out.Size() < queryCount)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: timestamp read needs a device and enough output slots");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const Status<Error> healthy = DeviceLostStatus(device);
    if (!healthy)
    {
        return healthy;
    }
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12TimestampQueryHeapSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: timestamp heap handle is stale or invalid");
    }
    if (firstQuery >= slot->desc.queryCount || queryCount > slot->desc.queryCount - firstQuery)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: timestamp read range exceeds the query heap");
    }
    D3D12FenceSlot* fence = Resolve(device, completionFence);
    if (fence == nullptr || fence->fence == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: timestamp read fence is stale or invalid");
    }
    const u64 completed = fence->fence->GetCompletedValue();
    if (completed == UINT64_MAX)
    {
        return DetectDeviceLost(device, DXGI_ERROR_DEVICE_REMOVED);
    }
    if (completed < completionValue)
    {
        return StatusError(ErrorCode::NotFound,
                           "d3d12 backend: timestamp completion fence has not reached its value");
    }
    const auto* values = static_cast<const u64*>(slot->mapped);
    for (u32 index = 0; index < queryCount; ++index)
    {
        out[index] = values[firstQuery + index];
    }
    return Status<Error>::Ok();
}

u64 D3D12TimestampFrequency(const void* internal, QueueKind kind) noexcept
{
    if (internal == nullptr || kind >= QueueKind::Count)
    {
        return 0;
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12Queue& queue = device.queues[static_cast<u32>(kind)];
    u64 frequency = 0;
    return queue.queue != nullptr && SUCCEEDED(queue.queue->GetTimestampFrequency(&frequency))
               ? frequency
               : 0;
}

u32 D3D12ReportLeaks(const void* internal) noexcept
{
    if (internal == nullptr)
    {
        return 0;
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    u32 count = 0;
    for (usize index = 0; index < device.buffers.Size(); ++index)
    {
        const D3D12BufferSlot& slot = device.buffers[index];
        if (slot.alive)
        {
            ++count;
            CG_LOG(Warning, Rhi, "live buffer %u generation %u: %s (%llu bytes)", index,
                   slot.generation,
                   slot.desc.label != nullptr && slot.desc.label[0] != '\0' ? slot.desc.label
                                                                            : "unlabelled",
                   static_cast<unsigned long long>(slot.desc.sizeBytes));
        }
    }
    for (usize index = 0; index < device.textures.Size(); ++index)
    {
        const D3D12TextureSlot& slot = device.textures[index];
        if (slot.alive && !slot.swapChainOwned)
        {
            ++count;
            CG_LOG(Warning, Rhi, "live texture %u generation %u: %s (%ux%u)", index,
                   slot.generation,
                   slot.desc.label != nullptr && slot.desc.label[0] != '\0' ? slot.desc.label
                                                                            : "unlabelled",
                   slot.desc.width, slot.desc.height);
        }
    }
    for (usize index = 0; index < device.timestampQueryHeaps.Size(); ++index)
    {
        const D3D12TimestampQueryHeapSlot& slot = device.timestampQueryHeaps[index];
        if (slot.alive)
        {
            ++count;
            CG_LOG(Warning, Rhi, "live timestamp heap %u generation %u: %s (%u queries)", index,
                   slot.generation,
                   slot.desc.label != nullptr && slot.desc.label[0] != '\0' ? slot.desc.label
                                                                            : "unlabelled",
                   slot.desc.queryCount);
        }
    }
    CG_LOG(Info, Rhi, "d3d12 backend: %u live resources", count);
    return count;
}

void ReleaseTimestampQueryHeaps(D3D12Device& device) noexcept
{
    for (usize index = 0; index < device.timestampQueryHeaps.Size(); ++index)
    {
        D3D12TimestampQueryHeapSlot& slot = device.timestampQueryHeaps[index];
        if (!slot.alive)
        {
            continue;
        }
        const D3D12_RANGE written{0, 0};
        if (slot.readback != nullptr)
        {
            if (slot.mapped != nullptr)
            {
                slot.readback->Unmap(0, &written);
            }
            slot.readback->Release();
        }
        if (slot.queries != nullptr)
        {
            slot.queries->Release();
        }
        device.memory.Deallocate(d3d12::MemoryClassType::Readback, slot.heap, slot.heapOffset,
                                 slot.reservedBytes, slot.dedicated);
        slot = D3D12TimestampQueryHeapSlot{};
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
    const Status<Error> healthy = DeviceLostStatus(device);
    if (!healthy)
    {
        return healthy;
    }
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (desc.sizeBytes > device.capabilities.limits.maxBufferBytes)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: buffer size is above Limits::maxBufferBytes");
    }
    if (desc.lifetime >= MemoryLifetime::Count)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: buffer lifetime is not a lifetime");
    }
    // Destroy runs the same drain before it decides where a reservation goes,
    // so a used-up epoch can never hide behind the one being created now.
    DrainTransientReclaims(device);

    const d3d12::MemoryClassType memoryClass = MemoryClassOf(desc.usage);

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
    // GENERIC_READ and a readback heap in COPY_DEST. The memory class decides the
    // heap type the resource is placed into, so the same rule follows from it.
    D3D12_RESOURCE_STATES initial = D3D12_RESOURCE_STATE_COMMON;
    if (memoryClass == d3d12::MemoryClassType::Upload)
    {
        initial = D3D12_RESOURCE_STATE_GENERIC_READ;
    }
    else if (memoryClass == d3d12::MemoryClassType::Readback)
    {
        initial = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    // Reserve space for the buffer inside its class's heaps (ADR 0018):
    // sub-allocated when a block can hold it, dedicated when nothing can. The
    // resource is then created *placed*, not committed, which is what lets a
    // released allocation return to the free list instead of vanishing.
    ID3D12Heap* heap = nullptr;
    u64 heapOffset = 0;
    bool dedicated = false;
    if (auto allocated =
            device.memory.Allocate(memoryClass, desc.sizeBytes, d3d12::kDefaultPlacementAlignment,
                                   &heap, &heapOffset, &dedicated);
        allocated.IsError())
    {
        return StatusError(allocated.Error().code, allocated.Error().message);
    }

    ID3D12Resource* resource3D = nullptr;
    if (FAILED(device.device->CreatePlacedResource(heap, heapOffset, &resource, initial, nullptr,
                                                   __uuidof(ID3D12Resource),
                                                   reinterpret_cast<void**>(&resource3D))) ||
        resource3D == nullptr)
    {
        device.memory.Deallocate(
            memoryClass, heap, heapOffset,
            cyber::memory::AlignUp(desc.sizeBytes, d3d12::kDefaultPlacementAlignment), dedicated);
        return StatusError(ErrorCode::Internal, "d3d12 backend: buffer creation failed");
    }
    NameObject(resource3D, desc.label);

    void* mapped = nullptr;
    if (initial == D3D12_RESOURCE_STATE_GENERIC_READ || initial == D3D12_RESOURCE_STATE_COPY_DEST)
    {
        // A host heap stays mapped for its lifetime: mapping per write costs more
        // than the write on a write-combined heap.
        const D3D12_RANGE range{0, 0};
        if (FAILED(resource3D->Map(0, &range, &mapped)))
        {
            resource3D->Release();
            device.memory.Deallocate(
                memoryClass, heap, heapOffset,
                cyber::memory::AlignUp(desc.sizeBytes, d3d12::kDefaultPlacementAlignment),
                dedicated);
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
    slot.heap = heap;
    slot.heapOffset = heapOffset;
    slot.reservedBytes = cyber::memory::AlignUp(desc.sizeBytes, d3d12::kDefaultPlacementAlignment);
    slot.memoryClass = memoryClass;
    slot.dedicated = dedicated;

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

    // The reservation goes back to the allocator rather than to the OS, so a
    // later allocation can land where this buffer was (ADR 0018). A transient
    // resource destroyed while work that may still touch it is in flight keeps
    // its reservation until the tracker passes its epoch, because handing the
    // bytes out early would be a real hazard, not a bookkeeping error.
    DrainTransientReclaims(device);
    if (slot->desc.lifetime == MemoryLifetime::Transient && device.transientTracker != nullptr &&
        device.transientTracker->GetCompletedValue() < device.transientEpoch)
    {
        D3D12Device::DeferredReclaim reclaim{};
        reclaim.memoryClass = slot->memoryClass;
        reclaim.heap = slot->heap;
        reclaim.offset = slot->heapOffset;
        reclaim.reservedBytes = slot->reservedBytes;
        reclaim.dedicated = slot->dedicated;
        reclaim.epoch = device.transientEpoch;
        if (!device.deferredReclaims.TryPushBack(reclaim))
        {
            // The bytes must still return; an entry that cannot be recorded
            // falls back to the immediate return the retained path uses.
            device.memory.Deallocate(slot->memoryClass, slot->heap, slot->heapOffset,
                                     slot->reservedBytes, slot->dedicated);
            slot->heap = nullptr;
            slot->heapOffset = 0;
            slot->reservedBytes = 0;
            slot->alive = false;
            slot->generation = NextGeneration(slot->generation);
            slot->state = ResourceState::Undefined;
            (void)device.freeBufferSlots.TryPushBack(handle.index);
            return StatusError(ErrorCode::Internal,
                               "d3d12 backend: transient reclaim bookkeeping failed");
        }
    }
    else
    {
        device.memory.Deallocate(slot->memoryClass, slot->heap, slot->heapOffset,
                                 slot->reservedBytes, slot->dedicated);
    }
    slot->heap = nullptr;
    slot->heapOffset = 0;
    slot->reservedBytes = 0;
    slot->alive = false;
    slot->generation = NextGeneration(slot->generation);
    slot->state = ResourceState::Undefined;
    if (!device.freeBufferSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: buffer free list growth failed");
    }
    return Status<Error>::Ok();
}

// Returns every deferred reservation the tracker has passed. Create and
// destroy call this under the device mutex, so `MemoryStats` never shows a
// reservation that is legally back.
void DrainTransientReclaims(D3D12Device& device) noexcept
{
    if (device.deferredReclaims.Empty())
    {
        return;
    }
    const u64 completed =
        device.transientTracker != nullptr ? device.transientTracker->GetCompletedValue() : 0;
    for (usize index = 0; index < device.deferredReclaims.Size();)
    {
        // Facts copied before the entry is replaced: the swap below overwrites
        // the slot that `reclaim` reads from.
        const d3d12::MemoryClassType memoryClass = device.deferredReclaims[index].memoryClass;
        ID3D12Heap* heap = device.deferredReclaims[index].heap;
        const u64 offset = device.deferredReclaims[index].offset;
        const u64 reservedBytes = device.deferredReclaims[index].reservedBytes;
        const bool dedicated = device.deferredReclaims[index].dedicated;
        if (device.deferredReclaims[index].epoch > completed)
        {
            ++index;
            continue;
        }
        device.memory.Deallocate(memoryClass, heap, offset, reservedBytes, dedicated);
        const usize last = device.deferredReclaims.Size() - 1;
        device.deferredReclaims[index] = device.deferredReclaims[last];
        (void)device.deferredReclaims.TryResize(last);
    }
}

// Teardown path: the device is leaving, so every remaining deferred
// reservation is returned without waiting.
void FlushTransientReclaims(D3D12Device& device) noexcept
{
    for (usize index = 0; index < device.deferredReclaims.Size(); ++index)
    {
        const D3D12Device::DeferredReclaim& reclaim = device.deferredReclaims[index];
        device.memory.Deallocate(reclaim.memoryClass, reclaim.heap, reclaim.offset,
                                 reclaim.reservedBytes, reclaim.dedicated);
    }
    device.deferredReclaims.Clear();
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
    info.lifetime = slot->desc.lifetime;
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
    const Status<Error> healthy = DeviceLostStatus(device);
    if (!healthy)
    {
        return healthy;
    }
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
    const Status<Error> healthy = DeviceLostStatus(device);
    if (!healthy)
    {
        return healthy;
    }
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
