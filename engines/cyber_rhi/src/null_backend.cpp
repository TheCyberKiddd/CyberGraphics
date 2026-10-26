// The null backend: a real backend that drives no hardware.
//
// It exists so the interface can be exercised, so a headless run and a test can
// use the same code path as a GPU run, and so validation has a home that does not
// depend on a driver. It allocates nothing on a GPU, keeps its state in one
// allocator-backed object per device, and really implements timeline fences so a
// wait that should block does block.
//
// Resources follow the same principle. A host-visible buffer owns real bytes, so
// `WriteBuffer` followed by `ReadBuffer` round-trips; every other resource stores
// only its descriptor and its tracked state. The point is to check the contract a
// caller is held to, not to pretend to be a GPU.

#include <cyber/memory/alignment.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/slot_map.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/texture.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <utility>

namespace cyber::rhi
{

namespace
{

using cyber::core::ErrorCode;
using cyber::core::usize;
using cyber::memory::SlotMap;

[[nodiscard]] Status<Error> StatusError(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

[[nodiscard]] bool Fail(Error* out, ErrorCode code, const char* message) noexcept
{
    if (out != nullptr)
    {
        out->code = code;
        out->message = message;
    }
    return false;
}

// One slot of the fence table. `generation` only increases, and 0 means the slot
// has never been used, so a valid handle always has a non-zero generation.
struct NullFenceSlot
{
    u64 value = 0;
    u32 generation = 0;
    bool alive = false;
};

// A buffer's record. `hostData` is non-null only for a host-visible or readback
// buffer, and is owned by the device's allocation list rather than by this
// record, so a device teardown cannot leak it.
struct NullBuffer
{
    BufferDesc desc{};
    memory::Byte* hostData = nullptr;
    ResourceState state = ResourceState::Undefined;
};

// A texture's record. `states` holds one entry per `(mip, array layer)` and is
// likewise owned by the device's allocation list.
struct NullTexture
{
    TextureDesc desc{};
    ResourceState* states = nullptr;
};

// A view borrows its texture; its descriptor already names the texture handle.
struct NullView
{
    TextureHandle texture;
    TextureViewDesc desc{};
};

struct NullSampler
{
    SamplerDesc desc{};
};

// A shader module keeps the description, including the borrowed code span, and
// the values a query reports. The caller guarantees the code outlives the
// module, which is the contract `ShaderDesc` documents.
struct NullShader
{
    ShaderInfo info{};
};

// A layout owns a copy of its entries, so a caller's stack array can die after
// creation and a bind group can still be validated against it.
struct NullBindGroupLayout
{
    BindGroupLayoutEntry* entries = nullptr;
    u32 entryCount = 0;
    u32 resourceCount = 0;
};

// A group owns a copy of its resources and records the layout it was made from.
struct NullBindGroup
{
    BindGroupLayoutHandle layout;
    BindingResource* resources = nullptr;
    u32 resourceCount = 0;
};

// A pipeline layout owns its group handles and a copy of its push-constant
// ranges.
struct NullPipelineLayout
{
    BindGroupLayoutHandle groups[kBindGroupSlotCount] = {};
    PushConstantRange* pushConstants = nullptr;
    u32 pushConstantCount = 0;
};

struct NullPipeline
{
    PipelineKind kind = PipelineKind::Graphics;
    u64 hash = 0;
};

// The null backend compiles nothing, so a cache only remembers how many seed
// bytes it was given and always saves nothing.
struct NullPipelineCache
{
    u64 seedBytes = 0;
};

struct NullDevice
{
    explicit NullDevice(memory::Allocator& allocator) noexcept
        : allocator(&allocator), fences(allocator), freeSlots(allocator), buffers(allocator),
          textures(allocator), views(allocator), samplers(allocator), shaders(allocator),
          bindGroupLayouts(allocator), bindGroups(allocator), pipelineLayouts(allocator),
          pipelines(allocator), pipelineCaches(allocator), liveViews(allocator),
          allocations(allocator)
    {
    }

    ~NullDevice()
    {
        // A caller may destroy a device while resources are still alive. The
        // device owns their storage, so releasing it here is the documented
        // behaviour and keeps the allocator accounting honest.
        for (usize index = 0; index < allocations.Size(); ++index)
        {
            allocator->Deallocate(allocations[index]);
        }
        allocations.Clear();
    }

    memory::Allocator* allocator = nullptr;
    // One lock for the object tables and the fence timelines. Coarse on purpose:
    // this backend exists to validate, and Phase 14 measures submission rather
    // than creation.
    mutable std::mutex mutex;
    std::condition_variable condition;
    memory::DynamicArray<NullFenceSlot> fences;
    memory::DynamicArray<u32> freeSlots;
    SlotMap<NullBuffer, BufferTag> buffers;
    SlotMap<NullTexture, TextureTag> textures;
    SlotMap<NullView, TextureViewTag> views;
    SlotMap<NullSampler, SamplerTag> samplers;
    SlotMap<NullShader, ShaderTag> shaders;
    SlotMap<NullBindGroupLayout, BindGroupLayoutTag> bindGroupLayouts;
    SlotMap<NullBindGroup, BindGroupTag> bindGroups;
    SlotMap<NullPipelineLayout, PipelineLayoutTag> pipelineLayouts;
    SlotMap<NullPipeline, PipelineTag> pipelines;
    SlotMap<NullPipelineCache, PipelineCacheTag> pipelineCaches;
    // Every live view, so destroying a texture can retire the views over it.
    memory::DynamicArray<TextureViewHandle> liveViews;
    // Host-visible bytes and per-subresource state arrays, released when the
    // resource or the device dies.
    memory::DynamicArray<void*> allocations;
    QueueHandle queues[kQueueKindCount] = {};
    Capabilities capabilities{};
};

// Resolves a handle to its slot, or null when the handle is stale, out of range,
// or belongs to a destroyed object.
[[nodiscard]] NullFenceSlot* ResolveFence(NullDevice& device, FenceHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.fences.Size())
    {
        return nullptr;
    }
    NullFenceSlot& slot = device.fences[handle.index];
    if (!slot.alive || slot.generation != handle.generation)
    {
        return nullptr;
    }
    return &slot;
}

[[nodiscard]] const NullFenceSlot* ResolveFence(const NullDevice& device,
                                                FenceHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.fences.Size())
    {
        return nullptr;
    }
    const NullFenceSlot& slot = device.fences[handle.index];
    if (!slot.alive || slot.generation != handle.generation)
    {
        return nullptr;
    }
    return &slot;
}

// Next generation for a slot, skipping zero so a valid handle is always
// distinguishable from a default one.
[[nodiscard]] u32 NextGeneration(u32 current) noexcept
{
    const u32 next = current + 1;
    return next == 0 ? 1 : next;
}

void CopyName(char (&destination)[kAdapterNameBytes], const char* source) noexcept
{
    u32 index = 0;
    if (source != nullptr)
    {
        while (source[index] != '\0' && index + 1 < kAdapterNameBytes)
        {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

[[nodiscard]] Capabilities MakeNullCapabilities() noexcept
{
    Capabilities capabilities{};
    CopyName(capabilities.adapter.name, "CyberGraphics Null Device");
    capabilities.adapter.backend = BackendKind::Null;
    capabilities.adapter.apiVersion = 0;
    capabilities.adapter.dedicatedVideoMemoryBytes = 0;
    capabilities.adapter.sharedSystemMemoryBytes = 0;

    Limits& limits = capabilities.limits;
    // Conservative, internally consistent maxima. They are what Phase 11
    // validation compares descriptors against, so they must be Linux-and-Windows
    // safe rather than impressive.
    limits.maxTexture1D = 8192;
    limits.maxTexture2D = 4096;
    limits.maxTexture3D = 2048;
    limits.maxTextureCube = 4096;
    limits.maxTextureArrayLayers = 256;
    limits.maxColorAttachments = 8;
    limits.maxVertexBuffers = 16;
    limits.maxVertexAttributes = 16;
    limits.maxBindGroups = 4;
    limits.maxBindingsPerGroup = 16;
    limits.maxPushConstantBytes = 128;
    limits.maxSamplerAnisotropy = 16;
    limits.minUniformBufferOffsetAlignment = 256;
    limits.minStorageBufferOffsetAlignment = 16;
    limits.maxBufferBytes = 1024ULL * 1024ULL * 1024ULL;

    // A validation backend must be able to accept the calls it is meant to
    // validate, so it reports the features the coming phases need, not the
    // features a piece of hardware happens to have. Storage textures and
    // anisotropic filtering are core on every target this project ships to;
    // reporting them here keeps the valid path testable, and the `Unsupported`
    // branch is exercised by the D3D12 backend's conformance tests in Phase 14.
    capabilities.features = Features::Of(Feature::Compute) | Feature::IndirectDraw |
                            Feature::TimelineFences | Feature::DebugMarkers |
                            Feature::StorageTextures | Feature::AnisotropicFiltering;
    return capabilities;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

// Usages that make a buffer bindable to a pipeline. Neither host-visible nor
// readback may combine with any of them.
inline constexpr BufferUsage kBufferPipelineUsages = BufferUsage::Vertex | BufferUsage::Index |
                                                     BufferUsage::Uniform | BufferUsage::Storage |
                                                     BufferUsage::Indirect;

[[nodiscard]] bool ValidateBufferDesc(const Limits& limits, const BufferDesc& desc,
                                      Error* out) noexcept
{
    if (desc.usage == BufferUsage::None)
    {
        return Fail(out, ErrorCode::InvalidArgument, "buffer descriptor has no usage");
    }
    if (desc.sizeBytes == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "buffer size is zero");
    }
    if (desc.sizeBytes > limits.maxBufferBytes)
    {
        return Fail(out, ErrorCode::OutOfRange, "buffer size exceeds the device limit");
    }
    const bool hostVisible = HasUsage(desc.usage, BufferUsage::HostVisible);
    const bool readback = HasUsage(desc.usage, BufferUsage::Readback);
    if (hostVisible && readback)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a buffer cannot be both host visible and readback");
    }
    if ((hostVisible || readback) && (desc.usage & kBufferPipelineUsages) != BufferUsage::None)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a host visible or readback buffer cannot be bound to a pipeline");
    }
    if (desc.elementStride != 0 && (desc.elementStride % 4 != 0 || desc.elementStride > 2048))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "buffer element stride must be a multiple of 4 and at most 2048 bytes");
    }
    return true;
}

// Shape rules shared by a texture and a view of it.
[[nodiscard]] bool ValidateTextureShape(const Limits& limits, const TextureDesc& desc,
                                        Error* out) noexcept
{
    if (desc.dimension == TextureDimension::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture dimension is not a dimension");
    }
    const FormatInfo format = FormatInfoOf(desc.format);
    if (format.bytesPerBlock == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture format is not a format");
    }
    if (desc.usage == TextureUsage::None)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture descriptor has no usage");
    }
    if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture extent has a zero dimension");
    }
    if (desc.arrayLayers == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture has zero array layers");
    }
    if (desc.mipLevels == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture has zero mip levels");
    }

    switch (desc.dimension)
    {
    case TextureDimension::Dim1D:
        if (desc.height != 1 || desc.depth != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a 1d texture has height and depth of one");
        }
        break;
    case TextureDimension::Dim2D:
        if (desc.depth != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument, "a 2d texture has a depth of one");
        }
        break;
    case TextureDimension::Dim3D:
        if (desc.arrayLayers != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a 3d texture has exactly one array layer");
        }
        break;
    case TextureDimension::Cube:
        if (desc.depth != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument, "a cube texture has a depth of one");
        }
        if (desc.width != desc.height)
        {
            return Fail(out, ErrorCode::InvalidArgument, "a cube texture must be square");
        }
        if (desc.arrayLayers % 6 != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a cube texture layer count is a multiple of six");
        }
        break;
    case TextureDimension::Count:
        break;
    }

    const u32 maxMips = MaxMipLevels(desc.dimension, desc.width, desc.height, desc.depth);
    if (desc.mipLevels > maxMips)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "texture mip count exceeds the chain its extent allows");
    }

    // Limits are a separate code so a caller that read the capabilities can tell
    // "I asked for too much" from "I described something impossible".
    if (desc.dimension == TextureDimension::Dim1D && desc.width > limits.maxTexture1D)
    {
        return Fail(out, ErrorCode::OutOfRange, "1d texture width exceeds the device limit");
    }
    if (desc.dimension == TextureDimension::Dim2D &&
        (desc.width > limits.maxTexture2D || desc.height > limits.maxTexture2D))
    {
        return Fail(out, ErrorCode::OutOfRange, "2d texture extent exceeds the device limit");
    }
    if (desc.dimension == TextureDimension::Cube &&
        (desc.width > limits.maxTextureCube || desc.height > limits.maxTextureCube))
    {
        return Fail(out, ErrorCode::OutOfRange, "cube texture extent exceeds the device limit");
    }
    if (desc.dimension == TextureDimension::Dim3D &&
        (desc.width > limits.maxTexture3D || desc.height > limits.maxTexture3D ||
         desc.depth > limits.maxTexture3D))
    {
        return Fail(out, ErrorCode::OutOfRange, "3d texture extent exceeds the device limit");
    }
    if (desc.arrayLayers > limits.maxTextureArrayLayers)
    {
        return Fail(out, ErrorCode::OutOfRange, "texture layer count exceeds the device limit");
    }
    return true;
}

[[nodiscard]] bool ValidateTextureDesc(const Capabilities& capabilities, const TextureDesc& desc,
                                       Error* out) noexcept
{
    if (!ValidateTextureShape(capabilities.limits, desc, out))
    {
        return false;
    }

    const FormatInfo format = FormatInfoOf(desc.format);
    const bool multisampled = desc.sampleCount > 1;
    if (desc.sampleCount != 1 && desc.sampleCount != 2 && desc.sampleCount != 4 &&
        desc.sampleCount != 8)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture sample count must be 1, 2, 4, or 8");
    }
    if (multisampled)
    {
        if (desc.mipLevels != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a multisampled texture has exactly one mip level");
        }
        if (desc.dimension == TextureDimension::Dim1D ||
            desc.dimension == TextureDimension::Dim3D || desc.dimension == TextureDimension::Cube)
        {
            return Fail(out, ErrorCode::InvalidArgument, "only a 2d texture may be multisampled");
        }
        if (!FormatSupportsMultisample(desc.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a block compressed format cannot be multisampled");
        }
    }

    const bool renderTarget = HasUsage(desc.usage, TextureUsage::RenderTarget);
    const bool depthStencil = HasUsage(desc.usage, TextureUsage::DepthStencil);
    if (renderTarget && depthStencil)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a texture cannot be both a color and a depth attachment");
    }
    if (renderTarget && !FormatSupportsRenderTarget(desc.format))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "the texture format cannot be a color render target");
    }
    if (depthStencil && !format.depth)
    {
        return Fail(out, ErrorCode::InvalidArgument, "depth-stencil usage requires a depth format");
    }
    // A depth format nobody reads is a descriptor mistake: it can be an
    // attachment, sampled, or a copy target, but it cannot be none of those.
    if (!depthStencil && format.depth && !HasUsage(desc.usage, TextureUsage::Sampled) &&
        !HasUsage(desc.usage, TextureUsage::CopySource) &&
        !HasUsage(desc.usage, TextureUsage::CopyDestination))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a depth texture needs depth-stencil, sampled, or copy usage");
    }
    if (HasUsage(desc.usage, TextureUsage::Storage))
    {
        if (!capabilities.features.Has(Feature::StorageTextures))
        {
            return Fail(out, ErrorCode::Unsupported,
                        "storage textures are not supported by this device");
        }
        if (!FormatSupportsStorage(desc.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "the texture format cannot be a storage texture");
        }
        if (desc.sampleCount != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a storage texture has exactly one sample");
        }
        if (desc.mipLevels != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a storage texture has exactly one mip level");
        }
    }
    return true;
}

// Usages that require a matching aspect. A color view of a depth format is
// rejected by `ResolveSubresourceRange`, so this only has to check the format.
[[nodiscard]] bool IsViewDimensionAllowed(TextureDimension base, TextureDimension view) noexcept
{
    switch (base)
    {
    case TextureDimension::Dim1D:
        return view == TextureDimension::Dim1D;
    case TextureDimension::Dim2D:
        return view == TextureDimension::Dim2D || view == TextureDimension::Cube;
    case TextureDimension::Dim3D:
        return view == TextureDimension::Dim3D;
    case TextureDimension::Cube:
        return view == TextureDimension::Dim2D || view == TextureDimension::Cube;
    case TextureDimension::Count:
        break;
    }
    return false;
}

[[nodiscard]] bool ValidateTextureView(const TextureInfo& info, const TextureViewDesc& desc,
                                       ResolvedRange* resolved, Error* out) noexcept
{
    if (desc.dimension == TextureDimension::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "view dimension is not a dimension");
    }
    if (!IsViewDimensionAllowed(info.dimension, desc.dimension))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "view dimension cannot be exposed by the texture's dimension");
    }
    if (!IsFormatViewCompatible(desc.format, info.format))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "view format is not compatible with the texture format");
    }
    const ResolvedRange range = ResolveSubresourceRange(info, desc.range);
    if (!range.valid)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "view range names a mip, layer, or aspect the texture does not have");
    }
    if (desc.dimension == TextureDimension::Cube)
    {
        if (info.width != info.height)
        {
            return Fail(out, ErrorCode::InvalidArgument, "a cube view requires a square texture");
        }
        if (range.layerCount % 6 != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a cube view range holds a multiple of six layers");
        }
    }
    if (desc.dimension == TextureDimension::Dim3D && range.layerCount != 1)
    {
        return Fail(out, ErrorCode::InvalidArgument, "a 3d view holds exactly one layer");
    }
    if (resolved != nullptr)
    {
        *resolved = range;
    }
    return true;
}

[[nodiscard]] bool IsFiniteNonNegative(float value, float maximum) noexcept
{
    return value >= 0.0f && value <= maximum;
}

[[nodiscard]] bool ValidateSamplerDesc(const Capabilities& capabilities, const SamplerDesc& desc,
                                       Error* out) noexcept
{
    if (desc.minFilter >= FilterMode::Count || desc.magFilter >= FilterMode::Count ||
        desc.mipmapFilter >= FilterMode::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "sampler filter is not a filter");
    }
    if (desc.addressU >= AddressMode::Count || desc.addressV >= AddressMode::Count ||
        desc.addressW >= AddressMode::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "sampler address mode is not an address mode");
    }
    if (desc.borderColor >= BorderColor::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "sampler border color is not a border color");
    }
    if (desc.compareFunction >= CompareFunction::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "sampler comparison is not a comparison function");
    }
    if (desc.maxAnisotropy == 0 || desc.maxAnisotropy > capabilities.limits.maxSamplerAnisotropy)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "sampler anisotropy is outside 1..Limits::maxSamplerAnisotropy");
    }
    if (!IsFiniteNonNegative(desc.minLod, kMaxSamplerLod) ||
        !IsFiniteNonNegative(desc.maxLod, kMaxSamplerLod))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "sampler LOD bounds must be finite, non-negative, and at most 1000");
    }
    if (desc.minLod > desc.maxLod)
    {
        return Fail(out, ErrorCode::InvalidArgument, "sampler minLod is above maxLod");
    }
    if (!(desc.mipLodBias >= -kMaxMipLodBias && desc.mipLodBias <= kMaxMipLodBias))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "sampler LOD bias is outside +/-16 or not finite");
    }
    if (desc.maxAnisotropy > 1)
    {
        if (desc.minFilter != FilterMode::Linear || desc.magFilter != FilterMode::Linear ||
            desc.mipmapFilter != FilterMode::Linear)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "anisotropic filtering requires linear min, mag, and mip filters");
        }
        if (!capabilities.features.Has(Feature::AnisotropicFiltering))
        {
            return Fail(out, ErrorCode::Unsupported,
                        "anisotropic filtering is not supported by this device");
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Allocation bookkeeping
// ---------------------------------------------------------------------------

void ReleaseAllocation(NullDevice& device, void* pointer) noexcept
{
    if (pointer == nullptr)
    {
        return;
    }
    for (usize index = 0; index < device.allocations.Size(); ++index)
    {
        if (device.allocations[index] == pointer)
        {
            device.allocations[index] = device.allocations[device.allocations.Size() - 1];
            (void)device.allocations.TryResize(device.allocations.Size() - 1);
            break;
        }
    }
    device.allocator->Deallocate(pointer);
}

// ---------------------------------------------------------------------------
// Fences
// ---------------------------------------------------------------------------

Capabilities NullCapabilities(const void* internal) noexcept
{
    if (internal == nullptr)
    {
        return Capabilities{};
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    return device.capabilities;
}

QueueHandle NullQueue(const void* internal, QueueKind kind) noexcept
{
    if (internal == nullptr)
    {
        return QueueHandle{};
    }
    const u32 index = static_cast<u32>(kind);
    if (index >= kQueueKindCount)
    {
        return QueueHandle{};
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    return device.queues[index];
}

Status<Error> NullCreateFence(void* internal, const FenceDesc& desc, FenceHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: fence creation needs a device and an output handle");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    u32 index = cyber::memory::kInvalidHandleIndex;
    if (device.freeSlots.Size() > 0)
    {
        index = device.freeSlots[device.freeSlots.Size() - 1];
        (void)device.freeSlots.TryResize(device.freeSlots.Size() - 1);
    }
    else
    {
        if (!device.fences.TryPushBack(NullFenceSlot{}))
        {
            return StatusError(ErrorCode::Internal, "null backend: fence table growth failed");
        }
        index = static_cast<u32>(device.fences.Size() - 1);
    }

    NullFenceSlot& slot = device.fences[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.value = desc.initialValue;

    FenceHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroyFence(void* internal, FenceHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        NullFenceSlot* slot = ResolveFence(device, handle);
        if (slot == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "null backend: fence handle is stale or invalid");
        }
        slot->alive = false;
        slot->generation = NextGeneration(slot->generation);
        slot->value = 0;
        if (!device.freeSlots.TryPushBack(handle.index))
        {
            return StatusError(ErrorCode::Internal, "null backend: fence free list growth failed");
        }
    }
    // A waiter on this fence must wake up and report the handle as stale rather
    // than block until its timeout.
    device.condition.notify_all();
    return Status<Error>::Ok();
}

Status<Error> NullSignalFence(void* internal, FenceHandle handle, u64 value) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        NullFenceSlot* slot = ResolveFence(device, handle);
        if (slot == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "null backend: fence handle is stale or invalid");
        }
        if (value <= slot->value)
        {
            // The timeline never moves backwards; a signal at or below the current
            // value is already satisfied.
            return Status<Error>::Ok();
        }
        slot->value = value;
    }
    device.condition.notify_all();
    return Status<Error>::Ok();
}

Status<Error> NullWaitFence(void* internal, FenceHandle handle, u64 value,
                            u64 timeoutNanoseconds) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    std::unique_lock<std::mutex> lock(device.mutex);

    if (ResolveFence(device, handle) == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: fence handle is stale or invalid");
    }

    const auto reached = [&device, handle, value]() noexcept
    {
        const NullFenceSlot* slot = ResolveFence(device, handle);
        return slot != nullptr && slot->value >= value;
    };
    const auto gone = [&device, handle]() noexcept
    { return ResolveFence(device, handle) == nullptr; };

    if (timeoutNanoseconds == 0)
    {
        return reached() ? Status<Error>::Ok()
                         : StatusError(ErrorCode::NotFound,
                                       "null backend: fence has not reached the value");
    }

    const auto done = [&reached, &gone]() noexcept { return reached() || gone(); };
    if (timeoutNanoseconds == kInfiniteWaitNanoseconds)
    {
        device.condition.wait(lock, done);
    }
    else
    {
        (void)device.condition.wait_for(lock, std::chrono::nanoseconds(timeoutNanoseconds), done);
    }

    if (gone())
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: fence was destroyed while waiting");
    }
    if (!reached())
    {
        return StatusError(ErrorCode::NotFound,
                           "null backend: fence timed out before reaching the value");
    }
    return Status<Error>::Ok();
}

u64 NullFenceValue(const void* internal, FenceHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return 0;
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullFenceSlot* slot = ResolveFence(device, handle);
    return slot != nullptr ? slot->value : 0;
}

Status<Error> NullWaitIdle(void* internal) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    // Nothing is ever submitted, so there is nothing to drain.
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

Status<Error> NullCreateBuffer(void* internal, const BufferDesc& desc, BufferHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: buffer creation needs a device and an output handle");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    Error error{};
    if (!ValidateBufferDesc(device.capabilities.limits, desc, &error))
    {
        return Status<Error>::Err(error);
    }

    NullBuffer record;
    record.desc = desc;
    const bool hostAccess = HasUsage(desc.usage, BufferUsage::HostVisible) ||
                            HasUsage(desc.usage, BufferUsage::Readback);
    if (hostAccess)
    {
        void* memory = device.allocator->Allocate(static_cast<usize>(desc.sizeBytes),
                                                  cyber::memory::kDefaultAlignment);
        if (memory == nullptr)
        {
            return StatusError(ErrorCode::Internal, "null backend: host buffer allocation failed");
        }
        record.hostData = static_cast<memory::Byte*>(memory);
        if (!device.allocations.TryPushBack(memory))
        {
            device.allocator->Deallocate(memory);
            return StatusError(ErrorCode::Internal,
                               "null backend: buffer allocation list growth failed");
        }
    }

    memory::Byte* const hostData = record.hostData;
    const BufferHandle handle = device.buffers.Insert(std::move(record));
    if (!handle.IsValid())
    {
        ReleaseAllocation(device, hostData);
        return StatusError(ErrorCode::Internal, "null backend: buffer table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroyBuffer(void* internal, BufferHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    NullBuffer* buffer = device.buffers.Get(handle);
    if (buffer == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: buffer handle is stale or invalid");
    }
    memory::Byte* hostData = buffer->hostData;
    (void)device.buffers.Remove(handle);
    ReleaseAllocation(device, hostData);
    return Status<Error>::Ok();
}

BufferInfo NullBufferInfo(const void* internal, BufferHandle handle) noexcept
{
    BufferInfo info;
    if (internal == nullptr)
    {
        return info;
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullBuffer* buffer = device.buffers.Get(handle);
    if (buffer == nullptr)
    {
        return info;
    }
    info.valid = true;
    info.sizeBytes = buffer->desc.sizeBytes;
    info.elementStride = buffer->desc.elementStride;
    info.usage = buffer->desc.usage;
    return info;
}

[[nodiscard]] Status<Error> ValidateHostRange(const NullBuffer& buffer, u64 offset,
                                              usize size) noexcept
{
    if (offset > buffer.desc.sizeBytes || static_cast<u64>(size) > buffer.desc.sizeBytes - offset)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "null backend: host range leaves the buffer or overflows");
    }
    return Status<Error>::Ok();
}

Status<Error> NullWriteBuffer(void* internal, BufferHandle handle, u64 offset,
                              memory::Span<const memory::Byte> bytes) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    if (bytes.Data() == nullptr && !bytes.Empty())
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: write source is null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullBuffer* buffer = device.buffers.Get(handle);
    if (buffer == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: buffer handle is stale or invalid");
    }
    if (!HasUsage(buffer->desc.usage, BufferUsage::HostVisible))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: buffer is not host visible; it is not CPU writable");
    }
    const Status<Error> ranged = ValidateHostRange(*buffer, offset, bytes.Size());
    if (!ranged)
    {
        return ranged;
    }
    if (bytes.Empty())
    {
        return Status<Error>::Ok();
    }
    memory::Byte* destination = buffer->hostData + offset;
    for (usize index = 0; index < bytes.Size(); ++index)
    {
        destination[index] = bytes[index];
    }
    return Status<Error>::Ok();
}

Status<Error> NullReadBuffer(void* internal, BufferHandle handle, u64 offset,
                             memory::Span<memory::Byte> bytes) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    if (bytes.Data() == nullptr && !bytes.Empty())
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: read destination is null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullBuffer* buffer = device.buffers.Get(handle);
    if (buffer == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: buffer handle is stale or invalid");
    }
    // Both host-visible and readback memory is CPU-readable: an upload heap is
    // cached write-combined and slow to read, but readable, and a readback heap
    // exists to be read. Only a device-local buffer is not.
    if (!HasUsage(buffer->desc.usage, BufferUsage::Readback) &&
        !HasUsage(buffer->desc.usage, BufferUsage::HostVisible))
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: buffer is not CPU readable");
    }
    const Status<Error> ranged = ValidateHostRange(*buffer, offset, bytes.Size());
    if (!ranged)
    {
        return ranged;
    }
    for (usize index = 0; index < bytes.Size(); ++index)
    {
        bytes[index] = buffer->hostData[offset + index];
    }
    return Status<Error>::Ok();
}

ResourceState NullBufferState(const void* internal, BufferHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return ResourceState::Undefined;
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullBuffer* buffer = device.buffers.Get(handle);
    return buffer != nullptr ? buffer->state : ResourceState::Undefined;
}

// ---------------------------------------------------------------------------
// Textures and views
// ---------------------------------------------------------------------------

Status<Error> NullCreateTexture(void* internal, const TextureDesc& desc,
                                TextureHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: texture creation needs a device and an output handle");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    Error error{};
    if (!ValidateTextureDesc(device.capabilities, desc, &error))
    {
        return Status<Error>::Err(error);
    }

    NullTexture record;
    record.desc = desc;
    const u64 subresources = static_cast<u64>(desc.mipLevels) * desc.arrayLayers;
    const usize stateBytes = static_cast<usize>(subresources) * sizeof(ResourceState);
    void* memory = device.allocator->Allocate(stateBytes, alignof(ResourceState));
    if (memory == nullptr)
    {
        return StatusError(ErrorCode::Internal, "null backend: texture state allocation failed");
    }
    record.states = static_cast<ResourceState*>(memory);
    for (u32 index = 0; index < subresources; ++index)
    {
        record.states[index] = ResourceState::Undefined;
    }
    if (!device.allocations.TryPushBack(memory))
    {
        device.allocator->Deallocate(memory);
        return StatusError(ErrorCode::Internal,
                           "null backend: texture allocation list growth failed");
    }

    const TextureHandle handle = device.textures.Insert(std::move(record));
    if (!handle.IsValid())
    {
        ReleaseAllocation(device, memory);
        return StatusError(ErrorCode::Internal, "null backend: texture table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

// Retires every live view of `texture`. Called with the device lock held.
void RetireViewsOf(NullDevice& device, TextureHandle texture) noexcept
{
    for (usize index = 0; index < device.liveViews.Size();)
    {
        const TextureViewHandle view = device.liveViews[index];
        const NullView* record = device.views.Get(view);
        if (record != nullptr && record->texture == texture)
        {
            (void)device.views.Remove(view);
            device.liveViews[index] = device.liveViews[device.liveViews.Size() - 1];
            (void)device.liveViews.TryResize(device.liveViews.Size() - 1);
            continue;
        }
        ++index;
    }
}

Status<Error> NullDestroyTexture(void* internal, TextureHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    NullTexture* texture = device.textures.Get(handle);
    if (texture == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: texture handle is stale or invalid");
    }
    ResourceState* states = texture->states;
    // A view of a destroyed texture must stop resolving. The caller's handles
    // are invalidated here rather than left dangling at freed state.
    RetireViewsOf(device, handle);
    (void)device.textures.Remove(handle);
    ReleaseAllocation(device, states);
    return Status<Error>::Ok();
}

TextureInfo NullTextureInfo(const void* internal, TextureHandle handle) noexcept
{
    TextureInfo info;
    if (internal == nullptr)
    {
        return info;
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullTexture* texture = device.textures.Get(handle);
    if (texture == nullptr)
    {
        return info;
    }
    info.valid = true;
    info.dimension = texture->desc.dimension;
    info.format = texture->desc.format;
    info.usage = texture->desc.usage;
    info.width = texture->desc.width;
    info.height = texture->desc.height;
    info.depth = texture->desc.depth;
    info.arrayLayers = texture->desc.arrayLayers;
    info.mipLevels = texture->desc.mipLevels;
    info.sampleCount = texture->desc.sampleCount;
    info.sizeBytes = TextureByteSize(texture->desc);
    return info;
}

Status<Error> NullCreateTextureView(void* internal, const TextureViewDesc& desc,
                                    TextureViewHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: view creation needs a device and an output handle");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    const NullTexture* texture = device.textures.Get(desc.texture);
    if (texture == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: view texture handle is stale or invalid");
    }

    TextureInfo info;
    info.valid = true;
    info.dimension = texture->desc.dimension;
    info.format = texture->desc.format;
    info.width = texture->desc.width;
    info.height = texture->desc.height;
    info.arrayLayers = texture->desc.arrayLayers;
    info.mipLevels = texture->desc.mipLevels;

    ResolvedRange resolved{};
    Error error{};
    if (!ValidateTextureView(info, desc, &resolved, &error))
    {
        return Status<Error>::Err(error);
    }

    NullView record;
    record.texture = desc.texture;
    record.desc = desc;
    // Store the resolved range so a query reports what the view actually
    // covers, not the zero counts the caller wrote.
    record.desc.range.mipLevelCount = resolved.mipLevelCount;
    record.desc.range.layerCount = resolved.layerCount;

    const TextureViewHandle handle = device.views.Insert(std::move(record));
    if (!handle.IsValid())
    {
        return StatusError(ErrorCode::Internal, "null backend: view table growth failed");
    }
    if (!device.liveViews.TryPushBack(handle))
    {
        (void)device.views.Remove(handle);
        return StatusError(ErrorCode::Internal, "null backend: view list growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroyTextureView(void* internal, TextureViewHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (device.views.Get(handle) == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: view handle is stale or invalid");
    }
    (void)device.views.Remove(handle);
    for (usize index = 0; index < device.liveViews.Size(); ++index)
    {
        if (device.liveViews[index] == handle)
        {
            device.liveViews[index] = device.liveViews[device.liveViews.Size() - 1];
            (void)device.liveViews.TryResize(device.liveViews.Size() - 1);
            break;
        }
    }
    return Status<Error>::Ok();
}

TextureViewInfo NullTextureViewInfo(const void* internal, TextureViewHandle handle) noexcept
{
    TextureViewInfo info;
    if (internal == nullptr)
    {
        return info;
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullView* view = device.views.Get(handle);
    if (view == nullptr)
    {
        return info;
    }
    info.valid = true;
    info.format = view->desc.format;
    info.dimension = view->desc.dimension;
    info.range = view->desc.range;
    return info;
}

ResourceState NullTextureState(const void* internal, TextureHandle handle, u32 mipLevel,
                               u32 arrayLayer) noexcept
{
    if (internal == nullptr)
    {
        return ResourceState::Undefined;
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullTexture* texture = device.textures.Get(handle);
    if (texture == nullptr || mipLevel >= texture->desc.mipLevels ||
        arrayLayer >= texture->desc.arrayLayers)
    {
        return ResourceState::Undefined;
    }
    return texture->states[static_cast<usize>(mipLevel) * texture->desc.arrayLayers + arrayLayer];
}

// ---------------------------------------------------------------------------
// Samplers
// ---------------------------------------------------------------------------

Status<Error> NullCreateSampler(void* internal, const SamplerDesc& desc,
                                SamplerHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: sampler creation needs a device and an output handle");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    Error error{};
    if (!ValidateSamplerDesc(device.capabilities, desc, &error))
    {
        return Status<Error>::Err(error);
    }

    NullSampler record;
    record.desc = desc;
    const SamplerHandle handle = device.samplers.Insert(std::move(record));
    if (!handle.IsValid())
    {
        return StatusError(ErrorCode::Internal, "null backend: sampler table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroySampler(void* internal, SamplerHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (!device.samplers.Remove(handle))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: sampler handle is stale or invalid");
    }
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

[[nodiscard]] bool ValidateShaderDesc(const ShaderDesc& desc, Error* out) noexcept
{
    if (desc.stage >= ShaderStage::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "shader stage is not a stage");
    }
    if (desc.format >= ShaderBytecodeFormat::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "shader bytecode format is not a format");
    }
    if (desc.code.Data() == nullptr || desc.code.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument, "shader code is empty");
    }
    const bool sourceCode = desc.format == ShaderBytecodeFormat::HlslSource ||
                            desc.format == ShaderBytecodeFormat::MslSource;
    if (sourceCode && (desc.entryPoint == nullptr || desc.entryPoint[0] == '\0'))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a source shader needs a non-empty entry point");
    }
    return true;
}

Status<Error> NullCreateShader(void* internal, const ShaderDesc& desc, ShaderHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: shader creation needs a device and an output handle");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    Error error{};
    if (!ValidateShaderDesc(desc, &error))
    {
        return Status<Error>::Err(error);
    }

    NullShader record;
    record.info.valid = true;
    record.info.stage = desc.stage;
    record.info.format = desc.format;
    record.info.codeSize = desc.code.Size();
    const ShaderHandle handle = device.shaders.Insert(std::move(record));
    if (!handle.IsValid())
    {
        return StatusError(ErrorCode::Internal, "null backend: shader table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroyShader(void* internal, ShaderHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (!device.shaders.Remove(handle))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: shader handle is stale or invalid");
    }
    return Status<Error>::Ok();
}

ShaderInfo NullShaderInfo(const void* internal, ShaderHandle handle) noexcept
{
    ShaderInfo info;
    if (internal == nullptr)
    {
        return info;
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullShader* shader = device.shaders.Get(handle);
    return shader != nullptr ? shader->info : info;
}

// ---------------------------------------------------------------------------
// Bind groups
// ---------------------------------------------------------------------------

// Copies an array into device-owned storage so a layout or group outlives the
// caller's stack array. Returns null for an empty source; a non-empty source
// that cannot be allocated is a failure the caller reports as `Internal`.
template <typename T>
[[nodiscard]] T* CopyArray(NullDevice& device, memory::Span<const T> source) noexcept
{
    if (source.Empty())
    {
        return nullptr;
    }
    void* memory = device.allocator->Allocate(source.Size() * sizeof(T), alignof(T));
    if (memory == nullptr)
    {
        return nullptr;
    }
    if (!device.allocations.TryPushBack(memory))
    {
        device.allocator->Deallocate(memory);
        return nullptr;
    }
    T* copy = static_cast<T*>(memory);
    for (usize index = 0; index < source.Size(); ++index)
    {
        copy[index] = source[index];
    }
    return copy;
}

[[nodiscard]] bool ValidateBindGroupLayout(const Limits& limits, const BindGroupLayoutDesc& desc,
                                           u32* resourceCount, Error* out) noexcept
{
    if (desc.entries.Data() == nullptr || desc.entries.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a bind group layout needs at least one entry");
    }
    if (desc.entries.Size() > limits.maxBindingsPerGroup)
    {
        return Fail(out, ErrorCode::OutOfRange,
                    "bind group layout has more entries than the device allows");
    }

    u32 total = 0;
    for (usize index = 0; index < desc.entries.Size(); ++index)
    {
        const BindGroupLayoutEntry& entry = desc.entries[index];
        if (entry.type >= BindingType::Count)
        {
            return Fail(out, ErrorCode::InvalidArgument, "binding type is not a binding type");
        }
        if (entry.visibility == 0 || (entry.visibility & ~kAllShaderStages) != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "binding visibility must name at least one real stage");
        }
        if (entry.arrayCount == 0)
        {
            return Fail(out, ErrorCode::InvalidArgument, "binding array count is zero");
        }
        if (entry.binding >= limits.maxBindingsPerGroup)
        {
            return Fail(out, ErrorCode::OutOfRange, "binding number exceeds the device limit");
        }
        if (entry.arrayCount > limits.maxBindingsPerGroup)
        {
            return Fail(out, ErrorCode::OutOfRange, "binding array count exceeds the device limit");
        }
        for (usize other = 0; other < index; ++other)
        {
            if (desc.entries[other].binding == entry.binding)
            {
                return Fail(out, ErrorCode::InvalidArgument,
                            "bind group layout repeats a binding number");
            }
        }
        total += entry.arrayCount;
    }

    if (resourceCount != nullptr)
    {
        *resourceCount = total;
    }
    return true;
}

Status<Error> NullCreateBindGroupLayout(void* internal, const BindGroupLayoutDesc& desc,
                                        BindGroupLayoutHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: layout creation needs a device and an output handle");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    u32 resourceCount = 0;
    Error error{};
    if (!ValidateBindGroupLayout(device.capabilities.limits, desc, &resourceCount, &error))
    {
        return Status<Error>::Err(error);
    }

    NullBindGroupLayout record;
    record.entries = CopyArray(device, desc.entries);
    if (record.entries == nullptr)
    {
        return StatusError(ErrorCode::Internal, "null backend: layout entry allocation failed");
    }
    record.entryCount = static_cast<u32>(desc.entries.Size());
    record.resourceCount = resourceCount;

    const BindGroupLayoutHandle handle = device.bindGroupLayouts.Insert(std::move(record));
    if (!handle.IsValid())
    {
        ReleaseAllocation(device, record.entries);
        return StatusError(ErrorCode::Internal, "null backend: layout table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroyBindGroupLayout(void* internal, BindGroupLayoutHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    NullBindGroupLayout* layout = device.bindGroupLayouts.Get(handle);
    if (layout == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: layout handle is stale or invalid");
    }
    BindGroupLayoutEntry* entries = layout->entries;
    (void)device.bindGroupLayouts.Remove(handle);
    ReleaseAllocation(device, entries);
    return Status<Error>::Ok();
}

// Resolves a buffer binding: live buffer, the usage its type needs, an aligned
// offset, and a non-empty range that stays inside the buffer.
[[nodiscard]] Status<Error> ValidateBufferBinding(const NullDevice& device,
                                                  const BindingResource& resource,
                                                  BufferUsage requiredUsage, u32 alignment,
                                                  const char* usageMessage) noexcept
{
    const NullBuffer* buffer = device.buffers.Get(resource.buffer);
    if (buffer == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: binding names a stale buffer handle");
    }
    if (!HasUsage(buffer->desc.usage, requiredUsage))
    {
        return StatusError(ErrorCode::InvalidArgument, usageMessage);
    }
    if (alignment != 0 && resource.offset % alignment != 0)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: buffer binding offset is not aligned");
    }
    if (resource.offset >= buffer->desc.sizeBytes)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "null backend: buffer binding starts at or past the end");
    }
    const u64 rangeSize =
        resource.size == 0 ? buffer->desc.sizeBytes - resource.offset : resource.size;
    if (rangeSize == 0 || rangeSize > buffer->desc.sizeBytes - resource.offset)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "null backend: buffer binding range leaves the buffer");
    }
    return Status<Error>::Ok();
}

Status<Error> NullCreateBindGroup(void* internal, const BindGroupDesc& desc,
                                  BindGroupHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: group creation needs a device and an output handle");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    const NullBindGroupLayout* layout = device.bindGroupLayouts.Get(desc.layout);
    if (layout == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: group layout handle is stale or invalid");
    }
    if (desc.resources.Size() != layout->resourceCount)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: group resource count does not match its layout");
    }

    usize resourceIndex = 0;
    for (u32 entryIndex = 0; entryIndex < layout->entryCount; ++entryIndex)
    {
        const BindGroupLayoutEntry& entry = layout->entries[entryIndex];
        for (u32 element = 0; element < entry.arrayCount; ++element, ++resourceIndex)
        {
            const BindingResource& resource = desc.resources[resourceIndex];
            if (resource.type != entry.type)
            {
                return StatusError(ErrorCode::InvalidArgument,
                                   "null backend: group resource type does not match its layout");
            }
            switch (entry.type)
            {
            case BindingType::UniformBuffer:
            {
                const Status<Error> checked = ValidateBufferBinding(
                    device, resource, BufferUsage::Uniform,
                    device.capabilities.limits.minUniformBufferOffsetAlignment,
                    "null backend: a uniform binding needs a buffer with uniform usage");
                if (!checked)
                {
                    return checked;
                }
                break;
            }
            case BindingType::StorageBuffer:
            {
                const Status<Error> checked = ValidateBufferBinding(
                    device, resource, BufferUsage::Storage,
                    device.capabilities.limits.minStorageBufferOffsetAlignment,
                    "null backend: a storage binding needs a buffer with storage usage");
                if (!checked)
                {
                    return checked;
                }
                break;
            }
            case BindingType::SampledTexture:
            case BindingType::StorageTexture:
            {
                const NullView* view = device.views.Get(resource.textureView);
                if (view == nullptr)
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "null backend: binding names a stale view handle");
                }
                const NullTexture* texture = device.textures.Get(view->texture);
                if (texture == nullptr)
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "null backend: binding view has no live texture");
                }
                const TextureUsage needed = entry.type == BindingType::SampledTexture
                                                ? TextureUsage::Sampled
                                                : TextureUsage::Storage;
                if (!HasUsage(texture->desc.usage, needed))
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "null backend: texture usage does not fit the binding type");
                }
                break;
            }
            case BindingType::Sampler:
                if (device.samplers.Get(resource.sampler) == nullptr)
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "null backend: binding names a stale sampler handle");
                }
                break;
            case BindingType::Count:
                return StatusError(ErrorCode::InvalidArgument,
                                   "null backend: binding type is not a binding type");
            }
        }
    }

    NullBindGroup record;
    record.layout = desc.layout;
    record.resources = CopyArray(device, desc.resources);
    if (record.resources == nullptr)
    {
        return StatusError(ErrorCode::Internal, "null backend: group resource allocation failed");
    }
    record.resourceCount = static_cast<u32>(desc.resources.Size());

    const BindGroupHandle handle = device.bindGroups.Insert(std::move(record));
    if (!handle.IsValid())
    {
        ReleaseAllocation(device, record.resources);
        return StatusError(ErrorCode::Internal, "null backend: group table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroyBindGroup(void* internal, BindGroupHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    NullBindGroup* group = device.bindGroups.Get(handle);
    if (group == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: group handle is stale or invalid");
    }
    BindingResource* resources = group->resources;
    (void)device.bindGroups.Remove(handle);
    ReleaseAllocation(device, resources);
    return Status<Error>::Ok();
}

BindGroupInfo NullBindGroupInfo(const void* internal, BindGroupHandle handle) noexcept
{
    BindGroupInfo info;
    if (internal == nullptr)
    {
        return info;
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullBindGroup* group = device.bindGroups.Get(handle);
    if (group == nullptr)
    {
        return info;
    }
    info.valid = true;
    info.layout = group->layout;
    info.resourceCount = group->resourceCount;
    return info;
}

// ---------------------------------------------------------------------------
// Pipeline layouts and pipelines
// ---------------------------------------------------------------------------

[[nodiscard]] bool ValidatePipelineLayout(const Capabilities& capabilities,
                                          const NullDevice& device, const PipelineLayoutDesc& desc,
                                          Error* out) noexcept
{
    u32 usedSlots = 0;
    for (u32 slot = 0; slot < kBindGroupSlotCount; ++slot)
    {
        if (!desc.groups[slot].IsValid())
        {
            continue;
        }
        if (device.bindGroupLayouts.Get(desc.groups[slot]) == nullptr)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "pipeline layout names a stale group layout");
        }
        for (u32 other = 0; other < slot; ++other)
        {
            if (desc.groups[other] == desc.groups[slot])
            {
                return Fail(out, ErrorCode::InvalidArgument,
                            "pipeline layout repeats a group layout");
            }
        }
        ++usedSlots;
    }
    if (usedSlots == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "pipeline layout uses no group slot");
    }

    if (desc.pushConstants.Size() > kMaxPushConstantRanges)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "pipeline layout declares too many push constant ranges");
    }
    for (usize index = 0; index < desc.pushConstants.Size(); ++index)
    {
        const PushConstantRange& range = desc.pushConstants[index];
        if (range.stages == 0 || (range.stages & ~kAllShaderStages) != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "push constant range must name at least one real stage");
        }
        if (range.sizeBytes == 0 || range.offsetBytes % 4 != 0 || range.sizeBytes % 4 != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "push constant range is empty or not four byte aligned");
        }
        if (range.offsetBytes > capabilities.limits.maxPushConstantBytes ||
            range.sizeBytes > capabilities.limits.maxPushConstantBytes - range.offsetBytes)
        {
            return Fail(out, ErrorCode::OutOfRange, "push constant range exceeds the device limit");
        }
        for (usize other = 0; other < index; ++other)
        {
            const PushConstantRange& previous = desc.pushConstants[other];
            if ((previous.stages & range.stages) == 0)
            {
                continue;
            }
            const u32 previousEnd = previous.offsetBytes + previous.sizeBytes;
            const u32 end = range.offsetBytes + range.sizeBytes;
            if (range.offsetBytes < previousEnd && previous.offsetBytes < end)
            {
                return Fail(out, ErrorCode::InvalidArgument,
                            "push constant ranges overlap for a shared stage");
            }
        }
    }
    return true;
}

Status<Error> NullCreatePipelineLayout(void* internal, const PipelineLayoutDesc& desc,
                                       PipelineLayoutHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(
            ErrorCode::InvalidArgument,
            "null backend: pipeline layout creation needs a device and an output handle");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    Error error{};
    if (!ValidatePipelineLayout(device.capabilities, device, desc, &error))
    {
        return Status<Error>::Err(error);
    }

    NullPipelineLayout record;
    for (u32 slot = 0; slot < kBindGroupSlotCount; ++slot)
    {
        record.groups[slot] = desc.groups[slot];
    }
    record.pushConstants = CopyArray(device, desc.pushConstants);
    if (!desc.pushConstants.Empty() && record.pushConstants == nullptr)
    {
        return StatusError(ErrorCode::Internal, "null backend: push constant allocation failed");
    }
    record.pushConstantCount = static_cast<u32>(desc.pushConstants.Size());

    const PipelineLayoutHandle handle = device.pipelineLayouts.Insert(std::move(record));
    if (!handle.IsValid())
    {
        ReleaseAllocation(device, record.pushConstants);
        return StatusError(ErrorCode::Internal, "null backend: layout table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroyPipelineLayout(void* internal, PipelineLayoutHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    NullPipelineLayout* layout = device.pipelineLayouts.Get(handle);
    if (layout == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: pipeline layout handle is stale or invalid");
    }
    PushConstantRange* ranges = layout->pushConstants;
    (void)device.pipelineLayouts.Remove(handle);
    ReleaseAllocation(device, ranges);
    return Status<Error>::Ok();
}

[[nodiscard]] bool ValidateVertexLayout(const Capabilities& capabilities,
                                        const VertexLayout& layout, Error* out) noexcept
{
    if (layout.bindings.Data() == nullptr || layout.bindings.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument, "graphics pipeline has no vertex binding");
    }
    if (layout.attributes.Data() == nullptr || layout.attributes.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument, "graphics pipeline has no vertex attribute");
    }
    if (layout.bindings.Size() > capabilities.limits.maxVertexBuffers)
    {
        return Fail(out, ErrorCode::OutOfRange,
                    "graphics pipeline has more vertex bindings than the device allows");
    }
    if (layout.attributes.Size() > capabilities.limits.maxVertexAttributes)
    {
        return Fail(out, ErrorCode::OutOfRange,
                    "graphics pipeline has more vertex attributes than the device allows");
    }

    for (usize index = 0; index < layout.bindings.Size(); ++index)
    {
        const VertexBinding& binding = layout.bindings[index];
        if (binding.strideBytes == 0 || binding.strideBytes > kMaxVertexStrideBytes)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "vertex binding stride is zero or above 2048 bytes");
        }
        if (binding.rate >= VertexInputRate::Count)
        {
            return Fail(out, ErrorCode::InvalidArgument, "vertex input rate is not an input rate");
        }
    }

    for (usize index = 0; index < layout.attributes.Size(); ++index)
    {
        const VertexAttribute& attribute = layout.attributes[index];
        if (!IsVertexFormatSupported(attribute.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "vertex attribute format cannot be fetched as vertex input");
        }
        if (attribute.binding >= layout.bindings.Size())
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "vertex attribute names a binding that does not exist");
        }
        const u64 end =
            static_cast<u64>(attribute.offsetBytes) + FormatBytesPerBlock(attribute.format);
        if (end > layout.bindings[attribute.binding].strideBytes)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "vertex attribute extends past its binding stride");
        }
        for (usize other = 0; other < index; ++other)
        {
            if (layout.attributes[other].location == attribute.location)
            {
                return Fail(out, ErrorCode::InvalidArgument,
                            "vertex layout repeats an attribute location");
            }
        }
    }
    return true;
}

[[nodiscard]] bool ValidateBlendState(Format format, const BlendState& blend, Error* out) noexcept
{
    if (blend.writeMask > kColorWriteAll)
    {
        return Fail(out, ErrorCode::InvalidArgument, "blend write mask has bits above alpha");
    }
    if (!blend.enabled)
    {
        return true;
    }
    if (!FormatSupportsBlending(format))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "blending is not supported by the attachment format");
    }
    const BlendComponent* components[2] = {&blend.color, &blend.alpha};
    for (const BlendComponent* component : components)
    {
        if (component->source >= BlendFactor::Count ||
            component->destination >= BlendFactor::Count ||
            component->operation >= BlendOperation::Count)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "blend factor or operation is not an enumerator");
        }
    }
    return true;
}

[[nodiscard]] bool ValidateGraphicsPipelineDesc(const Capabilities& capabilities,
                                                const NullDevice& device,
                                                const GraphicsPipelineDesc& desc,
                                                Error* out) noexcept
{
    if (device.pipelineLayouts.Get(desc.layout) == nullptr)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline names a stale pipeline layout");
    }
    const NullShader* vertexShader = device.shaders.Get(desc.vertexShader);
    if (vertexShader == nullptr)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline names a stale vertex shader");
    }
    if (vertexShader->info.stage != ShaderStage::Vertex)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline vertex shader is not a vertex stage");
    }
    const NullShader* pixelShader = device.shaders.Get(desc.pixelShader);
    if (pixelShader == nullptr)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline names a stale pixel shader");
    }
    if (pixelShader->info.stage != ShaderStage::Pixel)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline pixel shader is not a pixel stage");
    }
    if (desc.cache.IsValid() && device.pipelineCaches.Get(desc.cache) == nullptr)
    {
        return Fail(out, ErrorCode::InvalidArgument, "graphics pipeline names a stale cache");
    }
    if (!ValidateVertexLayout(capabilities, desc.vertexLayout, out))
    {
        return false;
    }
    if (desc.topology >= PrimitiveTopology::Count || desc.rasterizer.cullMode >= CullMode::Count ||
        desc.rasterizer.frontFace >= FrontFace::Count ||
        desc.rasterizer.polygonMode >= PolygonMode::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "rasterizer or topology state is invalid");
    }
    if (desc.multisample.sampleCount != 1 && desc.multisample.sampleCount != 2 &&
        desc.multisample.sampleCount != 4 && desc.multisample.sampleCount != 8)
    {
        return Fail(out, ErrorCode::InvalidArgument, "pipeline sample count must be 1, 2, 4, or 8");
    }
    if (desc.multisample.alphaToCoverage && desc.multisample.sampleCount == 1)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "alpha to coverage needs more than one sample");
    }
    if (desc.depthStencil.depthCompare >= CompareFunction::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "depth comparison is not a comparison");
    }
    if (desc.depthStencil.stencilEnabled &&
        (desc.depthStencil.front.compare >= CompareFunction::Count ||
         desc.depthStencil.back.compare >= CompareFunction::Count ||
         desc.depthStencil.front.failOp >= StencilOperation::Count ||
         desc.depthStencil.front.depthFailOp >= StencilOperation::Count ||
         desc.depthStencil.front.passOp >= StencilOperation::Count ||
         desc.depthStencil.back.failOp >= StencilOperation::Count ||
         desc.depthStencil.back.depthFailOp >= StencilOperation::Count ||
         desc.depthStencil.back.passOp >= StencilOperation::Count))
    {
        return Fail(out, ErrorCode::InvalidArgument, "stencil state has an invalid enumerator");
    }

    const bool needsDepth = desc.depthStencil.depthTestEnabled ||
                            desc.depthStencil.depthWriteEnabled || desc.depthStencil.stencilEnabled;
    if (needsDepth && desc.depthStencilFormat == Format::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "pipeline has depth state but no depth attachment format");
    }
    if (desc.depthStencilFormat != Format::Count)
    {
        if (!IsDepthFormat(desc.depthStencilFormat))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "depth attachment format is not a depth format");
        }
        if (desc.depthStencil.stencilEnabled && !IsStencilFormat(desc.depthStencilFormat))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "stencil state needs a depth format with stencil");
        }
        if (desc.multisample.sampleCount > 1 && !FormatSupportsMultisample(desc.depthStencilFormat))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "depth attachment format cannot be multisampled");
        }
    }

    if (desc.colorAttachments.Data() == nullptr || desc.colorAttachments.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument, "graphics pipeline has no color attachment");
    }
    if (desc.colorAttachments.Size() > kMaxColorAttachments ||
        desc.colorAttachments.Size() > capabilities.limits.maxColorAttachments)
    {
        return Fail(out, ErrorCode::OutOfRange,
                    "graphics pipeline has more color attachments than the device allows");
    }
    for (usize index = 0; index < desc.colorAttachments.Size(); ++index)
    {
        const ColorAttachmentState& attachment = desc.colorAttachments[index];
        if (!FormatSupportsRenderTarget(attachment.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "color attachment format cannot be a render target");
        }
        if (desc.multisample.sampleCount > 1 && !FormatSupportsMultisample(attachment.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "color attachment format cannot be multisampled");
        }
        if (!ValidateBlendState(attachment.format, attachment.blend, out))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateComputePipelineDesc(const Capabilities& capabilities,
                                               const NullDevice& device,
                                               const ComputePipelineDesc& desc, Error* out) noexcept
{
    if (!capabilities.features.Has(Feature::Compute))
    {
        return Fail(out, ErrorCode::Unsupported, "compute pipelines are not supported");
    }
    if (device.pipelineLayouts.Get(desc.layout) == nullptr)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "compute pipeline names a stale pipeline layout");
    }
    const NullShader* shader = device.shaders.Get(desc.computeShader);
    if (shader == nullptr)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "compute pipeline names a stale compute shader");
    }
    if (shader->info.stage != ShaderStage::Compute)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "compute pipeline shader is not a compute stage");
    }
    if (desc.cache.IsValid() && device.pipelineCaches.Get(desc.cache) == nullptr)
    {
        return Fail(out, ErrorCode::InvalidArgument, "compute pipeline names a stale cache");
    }
    return true;
}

Status<Error> NullCreateGraphicsPipeline(void* internal, const GraphicsPipelineDesc& desc,
                                         PipelineHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: pipeline creation needs a device and an output handle");
    }
    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    Error error{};
    if (!ValidateGraphicsPipelineDesc(device.capabilities, device, desc, &error))
    {
        return Status<Error>::Err(error);
    }

    NullPipeline record;
    record.kind = PipelineKind::Graphics;
    record.hash = HashGraphicsPipelineDesc(desc);
    const PipelineHandle handle = device.pipelines.Insert(std::move(record));
    if (!handle.IsValid())
    {
        return StatusError(ErrorCode::Internal, "null backend: pipeline table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullCreateComputePipeline(void* internal, const ComputePipelineDesc& desc,
                                        PipelineHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: pipeline creation needs a device and an output handle");
    }
    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    Error error{};
    if (!ValidateComputePipelineDesc(device.capabilities, device, desc, &error))
    {
        return Status<Error>::Err(error);
    }

    NullPipeline record;
    record.kind = PipelineKind::Compute;
    record.hash = HashComputePipelineDesc(desc);
    const PipelineHandle handle = device.pipelines.Insert(std::move(record));
    if (!handle.IsValid())
    {
        return StatusError(ErrorCode::Internal, "null backend: pipeline table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroyPipeline(void* internal, PipelineHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (!device.pipelines.Remove(handle))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: pipeline handle is stale or invalid");
    }
    return Status<Error>::Ok();
}

PipelineInfo NullPipelineInfo(const void* internal, PipelineHandle handle) noexcept
{
    PipelineInfo info;
    if (internal == nullptr)
    {
        return info;
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const NullPipeline* pipeline = device.pipelines.Get(handle);
    if (pipeline == nullptr)
    {
        return info;
    }
    info.valid = true;
    info.kind = pipeline->kind;
    info.hash = pipeline->hash;
    return info;
}

Status<Error> NullCreatePipelineCache(void* internal, const PipelineCacheDesc& desc,
                                      PipelineCacheHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: cache creation needs a device and an output handle");
    }
    if (desc.initialData.Data() == nullptr && !desc.initialData.Empty())
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: cache seed bytes are null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    NullPipelineCache record;
    record.seedBytes = desc.initialData.Size();
    const PipelineCacheHandle handle = device.pipelineCaches.Insert(std::move(record));
    if (!handle.IsValid())
    {
        return StatusError(ErrorCode::Internal, "null backend: cache table growth failed");
    }
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> NullDestroyPipelineCache(void* internal, PipelineCacheHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (!device.pipelineCaches.Remove(handle))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: cache handle is stale or invalid");
    }
    return Status<Error>::Ok();
}

Status<Error> NullPipelineCacheBytes(const void* internal, PipelineCacheHandle handle,
                                     memory::Allocator& allocator,
                                     memory::DynamicArray<memory::Byte>* out) noexcept
{
    (void)allocator;
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }
    const auto& device = *static_cast<const NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (device.pipelineCaches.Get(handle) == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: cache handle is stale or invalid");
    }
    // Nothing is compiled here, so there is nothing to serialize. Saving an
    // empty cache is a success, which is what every backend must tolerate.
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Barriers
// ---------------------------------------------------------------------------

// Validates one buffer barrier against tracked state without changing anything.
[[nodiscard]] Status<Error> ValidateBufferBarrier(const NullDevice& device,
                                                  const BufferBarrier& barrier) noexcept
{
    const NullBuffer* buffer = device.buffers.Get(barrier.buffer);
    if (buffer == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: barrier names a stale buffer handle");
    }
    if (!IsValidStateSet(barrier.to))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: buffer barrier target is not a valid state set");
    }
    if (!IsBufferResourceState(barrier.to))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: buffer barrier target is not a buffer state");
    }
    if (barrier.from != ResourceState::Undefined)
    {
        if (!IsValidStateSet(barrier.from) || !IsBufferResourceState(barrier.from))
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "null backend: buffer barrier source is not a buffer state");
        }
        if (barrier.from != buffer->state)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "null backend: buffer barrier source does not match tracked state");
        }
    }
    return Status<Error>::Ok();
}

// Validates one texture barrier against tracked state for every subresource it
// covers, without changing anything.
[[nodiscard]] Status<Error> ValidateTextureBarrier(const NullDevice& device,
                                                   const TextureBarrier& barrier) noexcept
{
    const NullTexture* texture = device.textures.Get(barrier.texture);
    if (texture == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: barrier names a stale texture handle");
    }
    TextureInfo info;
    info.valid = true;
    info.dimension = texture->desc.dimension;
    info.format = texture->desc.format;
    info.width = texture->desc.width;
    info.height = texture->desc.height;
    info.arrayLayers = texture->desc.arrayLayers;
    info.mipLevels = texture->desc.mipLevels;
    const ResolvedRange range = ResolveSubresourceRange(info, barrier.range);
    if (!range.valid)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: texture barrier range leaves the texture");
    }
    if (!IsValidStateSet(barrier.to))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: texture barrier target is not a valid state set");
    }
    if (!IsTextureResourceState(barrier.to))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "null backend: texture barrier target is not a texture state");
    }

    const bool discard = barrier.from == ResourceState::Undefined;
    if (!discard)
    {
        if (!IsValidStateSet(barrier.from) || !IsTextureResourceState(barrier.from))
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "null backend: texture barrier source is not a texture state");
        }
    }

    for (u32 layer = range.baseArrayLayer; layer < range.baseArrayLayer + range.layerCount; ++layer)
    {
        for (u32 mip = range.baseMipLevel; mip < range.baseMipLevel + range.mipLevelCount; ++mip)
        {
            const ResourceState tracked =
                texture->states[static_cast<usize>(mip) * texture->desc.arrayLayers + layer];
            if (!discard && tracked != barrier.from)
            {
                return StatusError(
                    ErrorCode::InvalidArgument,
                    "null backend: texture barrier source does not match tracked state");
            }
        }
    }
    return Status<Error>::Ok();
}

Status<Error> NullTransition(void* internal, memory::Span<const BufferBarrier> buffers,
                             memory::Span<const TextureBarrier> textures) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "null backend: device is null");
    }

    auto& device = *static_cast<NullDevice*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    // Validate the whole batch first. A batch with one bad barrier changes
    // nothing, so a caller can fix the descriptor and resubmit without wondering
    // which half applied.
    for (usize index = 0; index < buffers.Size(); ++index)
    {
        const Status<Error> checked = ValidateBufferBarrier(device, buffers[index]);
        if (!checked)
        {
            return checked;
        }
    }
    for (usize index = 0; index < textures.Size(); ++index)
    {
        const Status<Error> checked = ValidateTextureBarrier(device, textures[index]);
        if (!checked)
        {
            return checked;
        }
    }

    for (usize index = 0; index < buffers.Size(); ++index)
    {
        NullBuffer* buffer = device.buffers.Get(buffers[index].buffer);
        if (buffer != nullptr)
        {
            buffer->state = buffers[index].to;
        }
    }
    for (usize index = 0; index < textures.Size(); ++index)
    {
        const TextureBarrier& barrier = textures[index];
        NullTexture* texture = device.textures.Get(barrier.texture);
        if (texture == nullptr)
        {
            continue;
        }
        TextureInfo info;
        info.valid = true;
        info.dimension = texture->desc.dimension;
        info.format = texture->desc.format;
        info.width = texture->desc.width;
        info.height = texture->desc.height;
        info.arrayLayers = texture->desc.arrayLayers;
        info.mipLevels = texture->desc.mipLevels;
        const ResolvedRange range = ResolveSubresourceRange(info, barrier.range);
        if (!range.valid)
        {
            continue;
        }
        for (u32 layer = range.baseArrayLayer; layer < range.baseArrayLayer + range.layerCount;
             ++layer)
        {
            for (u32 mip = range.baseMipLevel; mip < range.baseMipLevel + range.mipLevelCount;
                 ++mip)
            {
                texture->states[static_cast<usize>(mip) * texture->desc.arrayLayers + layer] =
                    barrier.to;
            }
        }
    }
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

u32 NullEnumerateAdapters(AdapterInfo* out, u32 capacity) noexcept
{
    if (out == nullptr || capacity == 0)
    {
        // Still report the count so a caller can size its buffer.
        return 1;
    }
    const Capabilities capabilities = MakeNullCapabilities();
    out[0] = capabilities.adapter;
    return 1;
}

[[nodiscard]] const DeviceInterface* NullDeviceInterface() noexcept;

Result<DeviceCreation, Error> NullCreateDevice(const DeviceDesc& desc,
                                               memory::Allocator& allocator) noexcept
{
    if (desc.adapterIndex != 0)
    {
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::OutOfRange,
                  "null backend: the null adapter is the only adapter and its index is 0"});
    }

    void* memory = allocator.Allocate(sizeof(NullDevice), alignof(NullDevice));
    if (memory == nullptr)
    {
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::Internal, "null backend: device allocation failed"});
    }

    auto* device = new (memory) NullDevice(allocator);
    device->capabilities = MakeNullCapabilities();
    for (u32 index = 0; index < kQueueKindCount; ++index)
    {
        QueueHandle queue;
        queue.index = index;
        queue.generation = 1;
        device->queues[index] = queue;
    }

    DeviceCreation creation;
    creation.interface = NullDeviceInterface();
    creation.internal = device;
    return Result<DeviceCreation, Error>::Ok(creation);
}

void NullDestroyDevice(void* internal) noexcept
{
    if (internal == nullptr)
    {
        return;
    }
    auto* device = static_cast<NullDevice*>(internal);
    memory::Allocator* allocator = device->allocator;
    device->~NullDevice();
    allocator->Deallocate(device);
}

[[nodiscard]] const DeviceInterface* NullDeviceInterface() noexcept
{
    static const DeviceInterface kInterface = []
    {
        DeviceInterface interface{};
        interface.capabilities = &NullCapabilities;
        interface.queue = &NullQueue;
        interface.createFence = &NullCreateFence;
        interface.destroyFence = &NullDestroyFence;
        interface.signalFence = &NullSignalFence;
        interface.waitFence = &NullWaitFence;
        interface.fenceValue = &NullFenceValue;
        interface.waitIdle = &NullWaitIdle;
        interface.createBuffer = &NullCreateBuffer;
        interface.destroyBuffer = &NullDestroyBuffer;
        interface.bufferInfo = &NullBufferInfo;
        interface.writeBuffer = &NullWriteBuffer;
        interface.readBuffer = &NullReadBuffer;
        interface.bufferState = &NullBufferState;
        interface.createTexture = &NullCreateTexture;
        interface.destroyTexture = &NullDestroyTexture;
        interface.textureInfo = &NullTextureInfo;
        interface.createTextureView = &NullCreateTextureView;
        interface.destroyTextureView = &NullDestroyTextureView;
        interface.textureViewInfo = &NullTextureViewInfo;
        interface.textureState = &NullTextureState;
        interface.createSampler = &NullCreateSampler;
        interface.destroySampler = &NullDestroySampler;
        interface.transition = &NullTransition;
        interface.createShader = &NullCreateShader;
        interface.destroyShader = &NullDestroyShader;
        interface.shaderInfo = &NullShaderInfo;
        interface.createBindGroupLayout = &NullCreateBindGroupLayout;
        interface.destroyBindGroupLayout = &NullDestroyBindGroupLayout;
        interface.createBindGroup = &NullCreateBindGroup;
        interface.destroyBindGroup = &NullDestroyBindGroup;
        interface.bindGroupInfo = &NullBindGroupInfo;
        interface.createPipelineLayout = &NullCreatePipelineLayout;
        interface.destroyPipelineLayout = &NullDestroyPipelineLayout;
        interface.createGraphicsPipeline = &NullCreateGraphicsPipeline;
        interface.destroyGraphicsPipeline = &NullDestroyPipeline;
        interface.createComputePipeline = &NullCreateComputePipeline;
        interface.destroyComputePipeline = &NullDestroyPipeline;
        interface.createPipelineCache = &NullCreatePipelineCache;
        interface.destroyPipelineCache = &NullDestroyPipelineCache;
        interface.pipelineCacheBytes = &NullPipelineCacheBytes;
        interface.pipelineInfo = &NullPipelineInfo;
        return interface;
    }();
    return &kInterface;
}

}  // namespace

const Backend* NullBackend() noexcept
{
    static const Backend kBackend = []
    {
        Backend backend{};
        backend.kind = BackendKind::Null;
        backend.name = "null";
        backend.apiVersion = 0;
        backend.interface = NullDeviceInterface();
        backend.enumerateAdapters = &NullEnumerateAdapters;
        backend.createDevice = &NullCreateDevice;
        backend.destroyDevice = &NullDestroyDevice;
        return backend;
    }();
    return &kBackend;
}

}  // namespace cyber::rhi
