#include <cyber/renderer/bindless_table.h>

#include <limits>
#include <utility>

namespace cyber::renderer
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::Status;
using cyber::memory::Span;

[[nodiscard]] Error TableError(ErrorCode code, const char* message) noexcept
{
    return Error{code, message};
}

[[nodiscard]] bool ValidFrame(const rhi::FrameRing& ring, const rhi::FrameSlot& frame,
                              u32 expectedFrames) noexcept
{
    return ring.IsValid() && ring.FramesInFlight() == expectedFrames && frame.valid &&
           frame.slot < expectedFrames && frame.frameIndex < ring.FrameIndex() &&
           frame.frameIndex % expectedFrames == frame.slot &&
           ring.SignalValueForFrame(frame.frameIndex) == frame.signalValue;
}

}  // namespace

ResourceBindingPath SelectResourceBindingPath(const rhi::Device& device) noexcept
{
    return device.IsValid() && device.Capabilities().features.Has(rhi::Feature::BindlessResources)
               ? ResourceBindingPath::Bindless
               : ResourceBindingPath::Conventional;
}

BindlessResourceTable::~BindlessResourceTable()
{
    (void)Destroy();
}

BindlessResourceTable::BindlessResourceTable(BindlessResourceTable&& other) noexcept
{
    MoveFrom(other);
}

BindlessResourceTable& BindlessResourceTable::operator=(BindlessResourceTable&& other) noexcept
{
    if (this != &other)
    {
        (void)Destroy();
        MoveFrom(other);
    }
    return *this;
}

Result<BindlessResourceTable, Error>
BindlessResourceTable::Create(rhi::Device& device, memory::Allocator& allocator,
                              const BindlessResourceTableDesc& desc) noexcept
{
    if (!device.IsValid())
    {
        return Result<BindlessResourceTable, Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: device is invalid"));
    }
    if (SelectResourceBindingPath(device) != ResourceBindingPath::Bindless)
    {
        return Result<BindlessResourceTable, Error>::Err(
            TableError(ErrorCode::Unsupported, "bindless table: device lacks bindless resources"));
    }
    if (desc.capacity == 0 || desc.framesInFlight == 0 ||
        desc.framesInFlight > rhi::kMaxFramesInFlight || !desc.fallbackTexture.IsValid() ||
        !desc.fallbackBuffer.IsValid() || !desc.sampler.IsValid() ||
        desc.capacity >
            (std::numeric_limits<cyber::core::usize>::max() / sizeof(rhi::BindingResource) - 1) / 2)
    {
        return Result<BindlessResourceTable, Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: invalid shape or fallback"));
    }

    BindlessResourceTable table{};
    table.m_device = &device;
    table.m_allocator = &allocator;
    table.m_capacity = desc.capacity;
    table.m_framesInFlight = desc.framesInFlight;
    table.m_storageElementStride = desc.storageElementStride;
    table.m_fallbackTexture = desc.fallbackTexture;
    table.m_fallbackBuffer = desc.fallbackBuffer;
    table.m_sampler = desc.sampler;

    const rhi::TextureViewInfo textureInfo = device.TextureViewInfoOf(desc.fallbackTexture);
    const rhi::BufferInfo bufferInfo = device.BufferInfoOf(desc.fallbackBuffer);
    if (!textureInfo.valid || !bufferInfo.valid ||
        !rhi::HasUsage(bufferInfo.usage, rhi::BufferUsage::Storage) ||
        bufferInfo.elementStride != desc.storageElementStride ||
        !table.ValidBuffer(desc.fallbackBuffer, 0, 0))
    {
        table.Reset();
        return Result<BindlessResourceTable, Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: fallback is not live"));
    }

    table.m_textures = static_cast<rhi::TextureViewHandle*>(allocator.Allocate(
        desc.capacity * sizeof(rhi::TextureViewHandle), alignof(rhi::TextureViewHandle)));
    table.m_buffers = static_cast<rhi::BindingResource*>(allocator.Allocate(
        desc.capacity * sizeof(rhi::BindingResource), alignof(rhi::BindingResource)));
    if (table.m_textures == nullptr || table.m_buffers == nullptr)
    {
        (void)table.Destroy();
        return Result<BindlessResourceTable, Error>::Err(
            TableError(ErrorCode::Internal, "bindless table: resource metadata allocation failed"));
    }
    for (u32 index = 0; index < desc.capacity; ++index)
    {
        table.m_textures[index] = desc.fallbackTexture;
        table.m_buffers[index] = rhi::BindingResource{
            rhi::BindingType::ReadOnlyStorageBuffer, desc.fallbackBuffer, 0, 0, {}, {}};
    }
    auto textureIndices = table.m_textureIndices.Initialize(desc.capacity, allocator);
    if (!textureIndices)
    {
        const Error error = textureIndices.Error();
        (void)table.Destroy();
        return Result<BindlessResourceTable, Error>::Err(error);
    }
    auto bufferIndices = table.m_bufferIndices.Initialize(desc.capacity, allocator);
    if (!bufferIndices)
    {
        const Error error = bufferIndices.Error();
        (void)table.Destroy();
        return Result<BindlessResourceTable, Error>::Err(error);
    }

    rhi::BindGroupLayoutEntry entries[3]{};
    entries[0].binding = 0;
    entries[0].type = rhi::BindingType::SampledTexture;
    entries[0].visibility = rhi::ShaderStageBit(rhi::ShaderStage::Vertex) |
                            rhi::ShaderStageBit(rhi::ShaderStage::Pixel) |
                            rhi::ShaderStageBit(rhi::ShaderStage::Compute);
    entries[0].arrayCount = desc.capacity;
    entries[1].binding = 1;
    entries[1].type = rhi::BindingType::ReadOnlyStorageBuffer;
    entries[1].visibility = entries[0].visibility;
    entries[1].arrayCount = desc.capacity;
    entries[2].binding = 2;
    entries[2].type = rhi::BindingType::Sampler;
    entries[2].visibility = entries[0].visibility;
    rhi::BindGroupLayoutDesc layoutDesc{};
    layoutDesc.entries = Span<const rhi::BindGroupLayoutEntry>(entries, 3);
    layoutDesc.label = "renderer bindless resources";
    auto layout = device.CreateBindGroupLayout(layoutDesc);
    if (!layout)
    {
        const Error error = layout.Error();
        (void)table.Destroy();
        return Result<BindlessResourceTable, Error>::Err(error);
    }
    table.m_layout = layout.Value();

    const cyber::core::usize resourceCount = static_cast<cyber::core::usize>(desc.capacity) * 2 + 1;
    auto* resources = static_cast<rhi::BindingResource*>(allocator.Allocate(
        resourceCount * sizeof(rhi::BindingResource), alignof(rhi::BindingResource)));
    if (resources == nullptr)
    {
        (void)table.Destroy();
        return Result<BindlessResourceTable, Error>::Err(
            TableError(ErrorCode::Internal, "bindless table: initial group allocation failed"));
    }
    for (cyber::core::usize index = 0; index < resourceCount; ++index)
    {
        resources[index] = {};
    }
    for (u32 index = 0; index < desc.capacity; ++index)
    {
        resources[index].type = rhi::BindingType::SampledTexture;
        resources[index].textureView = desc.fallbackTexture;
        resources[desc.capacity + index] = table.m_buffers[index];
    }
    resources[resourceCount - 1].type = rhi::BindingType::Sampler;
    resources[resourceCount - 1].sampler = desc.sampler;
    const rhi::BindGroupDesc groupDesc{table.m_layout,
                                       Span<const rhi::BindingResource>(resources, resourceCount),
                                       "renderer bindless frame table"};
    for (u32 slot = 0; slot < desc.framesInFlight; ++slot)
    {
        auto group = device.CreateBindGroup(groupDesc);
        if (!group)
        {
            const Error error = group.Error();
            allocator.Deallocate(resources);
            (void)table.Destroy();
            return Result<BindlessResourceTable, Error>::Err(error);
        }
        table.m_groups[slot] = group.Value();
        table.m_syncedRevisions[slot] = table.m_revision;
    }
    allocator.Deallocate(resources);
    return Result<BindlessResourceTable, Error>::Ok(std::move(table));
}

Result<PersistentDescriptor, Error>
BindlessResourceTable::AllocateTexture(rhi::TextureViewHandle view) noexcept
{
    if (!ValidTexture(view))
    {
        return Result<PersistentDescriptor, Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: sampled view is invalid"));
    }
    auto descriptor = m_textureIndices.Allocate();
    if (!descriptor)
    {
        return descriptor;
    }
    m_textures[descriptor.Value().index] = view;
    AdvanceRevision();
    return descriptor;
}

Status<Error> BindlessResourceTable::UpdateTexture(PersistentDescriptor descriptor,
                                                   rhi::TextureViewHandle view) noexcept
{
    if (!m_textureIndices.IsLive(descriptor) || !ValidTexture(view))
    {
        return Status<Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: stale texture or view"));
    }
    m_textures[descriptor.index] = view;
    AdvanceRevision();
    return Status<Error>::Ok();
}

Status<Error> BindlessResourceTable::FreeTexture(PersistentDescriptor descriptor) noexcept
{
    return FreeDesiredTexture(descriptor);
}

Result<PersistentDescriptor, Error> BindlessResourceTable::AllocateBuffer(rhi::BufferHandle buffer,
                                                                          u64 offsetBytes,
                                                                          u64 sizeBytes) noexcept
{
    if (!ValidBuffer(buffer, offsetBytes, sizeBytes))
    {
        return Result<PersistentDescriptor, Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: storage buffer is invalid"));
    }
    auto descriptor = m_bufferIndices.Allocate();
    if (!descriptor)
    {
        return descriptor;
    }
    m_buffers[descriptor.Value().index] = rhi::BindingResource{
        rhi::BindingType::ReadOnlyStorageBuffer, buffer, offsetBytes, sizeBytes};
    AdvanceRevision();
    return descriptor;
}

Status<Error> BindlessResourceTable::UpdateBuffer(PersistentDescriptor descriptor,
                                                  rhi::BufferHandle buffer, u64 offsetBytes,
                                                  u64 sizeBytes) noexcept
{
    if (!m_bufferIndices.IsLive(descriptor) || !ValidBuffer(buffer, offsetBytes, sizeBytes))
    {
        return Status<Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: stale buffer or resource"));
    }
    m_buffers[descriptor.index] = rhi::BindingResource{rhi::BindingType::ReadOnlyStorageBuffer,
                                                       buffer, offsetBytes, sizeBytes};
    AdvanceRevision();
    return Status<Error>::Ok();
}

Status<Error> BindlessResourceTable::FreeBuffer(PersistentDescriptor descriptor) noexcept
{
    return FreeDesiredBuffer(descriptor);
}

Status<Error> BindlessResourceTable::SyncFrame(const rhi::FrameRing& ring,
                                               const rhi::FrameSlot& frame) noexcept
{
    if (m_device == nullptr || m_allocator == nullptr || !ValidFrame(ring, frame, m_framesInFlight))
    {
        return Status<Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: invalid frame token"));
    }
    if (m_syncedRevisions[frame.slot] == m_revision)
    {
        return Status<Error>::Ok();
    }

    const cyber::core::usize resourceCount = static_cast<cyber::core::usize>(m_capacity) * 2 + 1;
    auto* resources = static_cast<rhi::BindingResource*>(m_allocator->Allocate(
        resourceCount * sizeof(rhi::BindingResource), alignof(rhi::BindingResource)));
    if (resources == nullptr)
    {
        return Status<Error>::Err(
            TableError(ErrorCode::Internal, "bindless table: frame update allocation failed"));
    }
    for (cyber::core::usize index = 0; index < resourceCount; ++index)
    {
        resources[index] = {};
    }
    for (u32 index = 0; index < m_capacity; ++index)
    {
        resources[index].type = rhi::BindingType::SampledTexture;
        resources[index].textureView = m_textures[index];
        resources[m_capacity + index] = m_buffers[index];
    }
    resources[resourceCount - 1].type = rhi::BindingType::Sampler;
    resources[resourceCount - 1].sampler = m_sampler;
    const Status<Error> updated = m_device->UpdateBindGroup(
        m_groups[frame.slot], Span<const rhi::BindingResource>(resources, resourceCount));
    m_allocator->Deallocate(resources);
    if (!updated)
    {
        return updated;
    }
    m_syncedRevisions[frame.slot] = m_revision;
    return Status<Error>::Ok();
}

rhi::BindGroupHandle BindlessResourceTable::Group(u32 slot) const noexcept
{
    return slot < m_framesInFlight ? m_groups[slot] : rhi::BindGroupHandle{};
}

rhi::BindGroupLayoutHandle BindlessResourceTable::Layout() const noexcept
{
    return m_layout;
}

u32 BindlessResourceTable::Capacity() const noexcept
{
    return m_capacity;
}

Status<Error> BindlessResourceTable::Destroy() noexcept
{
    Error firstError{};
    bool failed = false;
    if (m_device != nullptr)
    {
        for (u32 slot = 0; slot < m_framesInFlight; ++slot)
        {
            if (m_groups[slot].IsValid())
            {
                const Status<Error> destroyed = m_device->DestroyBindGroup(m_groups[slot]);
                if (!destroyed && !failed)
                {
                    firstError = destroyed.Error();
                    failed = true;
                }
            }
        }
        if (m_layout.IsValid())
        {
            const Status<Error> destroyed = m_device->DestroyBindGroupLayout(m_layout);
            if (!destroyed && !failed)
            {
                firstError = destroyed.Error();
                failed = true;
            }
        }
    }
    Reset();
    return failed ? Status<Error>::Err(firstError) : Status<Error>::Ok();
}

void BindlessResourceTable::MoveFrom(BindlessResourceTable& other) noexcept
{
    m_device = other.m_device;
    m_allocator = other.m_allocator;
    m_textures = other.m_textures;
    m_buffers = other.m_buffers;
    m_capacity = other.m_capacity;
    m_framesInFlight = other.m_framesInFlight;
    m_storageElementStride = other.m_storageElementStride;
    m_revision = other.m_revision;
    m_fallbackTexture = other.m_fallbackTexture;
    m_fallbackBuffer = other.m_fallbackBuffer;
    m_sampler = other.m_sampler;
    m_layout = other.m_layout;
    m_groups = other.m_groups;
    m_syncedRevisions = other.m_syncedRevisions;
    m_textureIndices = std::move(other.m_textureIndices);
    m_bufferIndices = std::move(other.m_bufferIndices);
    other.m_device = nullptr;
    other.m_allocator = nullptr;
    other.m_textures = nullptr;
    other.m_buffers = nullptr;
    other.m_capacity = 0;
    other.m_framesInFlight = 0;
    other.m_storageElementStride = 0;
    other.m_revision = 1;
    other.m_fallbackTexture = {};
    other.m_fallbackBuffer = {};
    other.m_sampler = {};
    other.m_layout = {};
    other.m_groups = {};
    other.m_syncedRevisions = {};
}

void BindlessResourceTable::Reset() noexcept
{
    if (m_allocator != nullptr)
    {
        m_allocator->Deallocate(m_textures);
        m_allocator->Deallocate(m_buffers);
    }
    m_textureIndices.Destroy();
    m_bufferIndices.Destroy();
    m_device = nullptr;
    m_allocator = nullptr;
    m_textures = nullptr;
    m_buffers = nullptr;
    m_capacity = 0;
    m_framesInFlight = 0;
    m_storageElementStride = 0;
    m_revision = 1;
    m_fallbackTexture = {};
    m_fallbackBuffer = {};
    m_sampler = {};
    m_layout = {};
    m_groups = {};
    m_syncedRevisions = {};
}

void BindlessResourceTable::AdvanceRevision() noexcept
{
    if (m_revision == std::numeric_limits<u64>::max())
    {
        m_syncedRevisions = {};
        m_revision = 1;
    }
    else
    {
        ++m_revision;
    }
}

bool BindlessResourceTable::ValidTexture(rhi::TextureViewHandle view) const noexcept
{
    return m_device != nullptr && m_device->TextureViewInfoOf(view).valid;
}

bool BindlessResourceTable::ValidBuffer(rhi::BufferHandle buffer, u64 offsetBytes,
                                        u64 sizeBytes) const noexcept
{
    if (m_device == nullptr)
    {
        return false;
    }
    const rhi::BufferInfo info = m_device->BufferInfoOf(buffer);
    if (!info.valid || !rhi::HasUsage(info.usage, rhi::BufferUsage::Storage) ||
        info.elementStride != m_storageElementStride || offsetBytes >= info.sizeBytes)
    {
        return false;
    }
    const u64 rangeSize = sizeBytes == 0 ? info.sizeBytes - offsetBytes : sizeBytes;
    const u32 apiAlignment = m_device->Capabilities().limits.minStorageBufferOffsetAlignment;
    return rangeSize != 0 && rangeSize <= info.sizeBytes - offsetBytes && apiAlignment != 0 &&
           offsetBytes % apiAlignment == 0 &&
           (info.elementStride == 0 || offsetBytes % info.elementStride == 0) &&
           (info.elementStride != 0 ? rangeSize % info.elementStride == 0 : rangeSize % 4 == 0);
}

Status<Error> BindlessResourceTable::FreeDesiredTexture(PersistentDescriptor descriptor) noexcept
{
    if (!m_textureIndices.IsLive(descriptor))
    {
        return Status<Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: stale texture index"));
    }
    const Status<Error> freed = m_textureIndices.Free(descriptor);
    if (!freed)
    {
        return freed;
    }
    m_textures[descriptor.index] = m_fallbackTexture;
    AdvanceRevision();
    return Status<Error>::Ok();
}

Status<Error> BindlessResourceTable::FreeDesiredBuffer(PersistentDescriptor descriptor) noexcept
{
    if (!m_bufferIndices.IsLive(descriptor))
    {
        return Status<Error>::Err(
            TableError(ErrorCode::InvalidArgument, "bindless table: stale buffer index"));
    }
    const Status<Error> freed = m_bufferIndices.Free(descriptor);
    if (!freed)
    {
        return freed;
    }
    m_buffers[descriptor.index] = rhi::BindingResource{
        rhi::BindingType::ReadOnlyStorageBuffer, m_fallbackBuffer, 0, 0, {}, {}};
    AdvanceRevision();
    return Status<Error>::Ok();
}

}  // namespace cyber::renderer
