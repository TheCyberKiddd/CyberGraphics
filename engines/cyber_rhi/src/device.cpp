#include <cyber/rhi/device.h>

#include <utility>

namespace cyber::rhi
{

namespace
{

[[nodiscard]] Result<Device, Error> DeviceError(core::ErrorCode code, const char* message) noexcept
{
    return Result<Device, Error>::Err(Error{code, message});
}

[[nodiscard]] Status<Error> StatusError(core::ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

// A backend that leaves an entry null is incomplete, and every public call
// would have to test the entry again. Checking once at creation is cheaper and
// makes the failure a device-creation error with one clear message.
[[nodiscard]] bool InterfaceIsComplete(const DeviceInterface& interface) noexcept
{
    return interface.capabilities != nullptr && interface.queue != nullptr &&
           interface.createFence != nullptr && interface.destroyFence != nullptr &&
           interface.signalFence != nullptr && interface.waitFence != nullptr &&
           interface.fenceValue != nullptr && interface.waitIdle != nullptr &&
           interface.createBuffer != nullptr && interface.destroyBuffer != nullptr &&
           interface.bufferInfo != nullptr && interface.writeBuffer != nullptr &&
           interface.readBuffer != nullptr && interface.bufferState != nullptr &&
           interface.createTexture != nullptr && interface.destroyTexture != nullptr &&
           interface.textureInfo != nullptr && interface.createTextureView != nullptr &&
           interface.destroyTextureView != nullptr && interface.textureViewInfo != nullptr &&
           interface.textureState != nullptr && interface.createSampler != nullptr &&
           interface.destroySampler != nullptr && interface.transition != nullptr;
}

}  // namespace

Device::~Device()
{
    Release();
}

Device::Device(Device&& other) noexcept
{
    MoveFrom(other);
}

Device& Device::operator=(Device&& other) noexcept
{
    if (this != &other)
    {
        Release();
        MoveFrom(other);
    }
    return *this;
}

Result<Device, Error> Device::Create(const DeviceDesc& desc, memory::Allocator& allocator) noexcept
{
    if (desc.backend == nullptr)
    {
        return DeviceError(core::ErrorCode::InvalidArgument, "device backend is null");
    }

    const Backend& backend = *desc.backend;
    if (backend.interface == nullptr || backend.createDevice == nullptr ||
        backend.destroyDevice == nullptr || backend.enumerateAdapters == nullptr)
    {
        return DeviceError(core::ErrorCode::InvalidArgument,
                           "backend is missing a required entry in its function table");
    }
    if (!InterfaceIsComplete(*backend.interface))
    {
        return DeviceError(core::ErrorCode::InvalidArgument,
                           "backend per-device table is missing a required entry");
    }

    auto created = backend.createDevice(desc, allocator);
    if (!created)
    {
        return DeviceError(created.Error().code, created.Error().message);
    }

    DeviceCreation creation = created.Value();
    if (creation.interface == nullptr || creation.internal == nullptr)
    {
        // A backend that fails halfway must still release what it made. Passing a
        // null internal is allowed so this path has one shape.
        backend.destroyDevice(creation.internal);
        return DeviceError(core::ErrorCode::Internal, "backend returned an incomplete device");
    }
    if (!InterfaceIsComplete(*creation.interface))
    {
        backend.destroyDevice(creation.internal);
        return DeviceError(core::ErrorCode::Internal,
                           "backend returned a device with an incomplete function table");
    }

    Device device;
    device.m_backend = &backend;
    device.m_interface = creation.interface;
    device.m_internal = creation.internal;
    return Result<Device, Error>::Ok(std::move(device));
}

bool Device::IsValid() const noexcept
{
    return m_interface != nullptr && m_internal != nullptr;
}

const char* Device::BackendName() const noexcept
{
    if (m_backend == nullptr)
    {
        return "none";
    }
    return m_backend->name;
}

// The member is named `Capabilities`, which hides the type name inside the
// class, so both the return type and the empty value are qualified here.
cyber::rhi::Capabilities Device::Capabilities() const noexcept
{
    if (!IsValid())
    {
        return cyber::rhi::Capabilities{};
    }
    return m_interface->capabilities(m_internal);
}

QueueHandle Device::Queue(QueueKind kind) const noexcept
{
    if (!IsValid() || m_interface->queue == nullptr)
    {
        return QueueHandle{};
    }
    return m_interface->queue(m_internal, kind);
}

Result<FenceHandle, Error> Device::CreateFence(const FenceDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createFence == nullptr)
    {
        return Result<FenceHandle, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no fence was created"});
    }

    FenceHandle fence;
    const Status<Error> created = m_interface->createFence(m_internal, desc, &fence);
    if (!created)
    {
        return Result<FenceHandle, Error>::Err(created.Error());
    }
    if (!fence.IsValid())
    {
        return Result<FenceHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid fence handle"});
    }
    return Result<FenceHandle, Error>::Ok(fence);
}

Status<Error> Device::DestroyFence(FenceHandle fence) noexcept
{
    if (!IsValid() || m_interface->destroyFence == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyFence(m_internal, fence);
}

Status<Error> Device::SignalFence(FenceHandle fence, u64 value) noexcept
{
    if (!IsValid() || m_interface->signalFence == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->signalFence(m_internal, fence, value);
}

Status<Error> Device::WaitFence(FenceHandle fence, u64 value, u64 timeoutNanoseconds) noexcept
{
    if (!IsValid() || m_interface->waitFence == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->waitFence(m_internal, fence, value, timeoutNanoseconds);
}

u64 Device::FenceValue(FenceHandle fence) const noexcept
{
    if (!IsValid() || m_interface->fenceValue == nullptr)
    {
        return 0;
    }
    return m_interface->fenceValue(m_internal, fence);
}

Result<BufferHandle, Error> Device::CreateBuffer(const BufferDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createBuffer == nullptr)
    {
        return Result<BufferHandle, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no buffer was created"});
    }

    BufferHandle buffer;
    const Status<Error> created = m_interface->createBuffer(m_internal, desc, &buffer);
    if (!created)
    {
        return Result<BufferHandle, Error>::Err(created.Error());
    }
    if (!buffer.IsValid())
    {
        return Result<BufferHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid buffer handle"});
    }
    return Result<BufferHandle, Error>::Ok(buffer);
}

Status<Error> Device::DestroyBuffer(BufferHandle buffer) noexcept
{
    if (!IsValid() || m_interface->destroyBuffer == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyBuffer(m_internal, buffer);
}

BufferInfo Device::BufferInfoOf(BufferHandle buffer) const noexcept
{
    if (!IsValid() || m_interface->bufferInfo == nullptr)
    {
        return BufferInfo{};
    }
    return m_interface->bufferInfo(m_internal, buffer);
}

Status<Error> Device::WriteBuffer(BufferHandle buffer, u64 offset,
                                  memory::Span<const memory::Byte> bytes) noexcept
{
    if (!IsValid() || m_interface->writeBuffer == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->writeBuffer(m_internal, buffer, offset, bytes);
}

Status<Error> Device::ReadBuffer(BufferHandle buffer, u64 offset,
                                 memory::Span<memory::Byte> bytes) noexcept
{
    if (!IsValid() || m_interface->readBuffer == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->readBuffer(m_internal, buffer, offset, bytes);
}

ResourceState Device::BufferStateOf(BufferHandle buffer) const noexcept
{
    if (!IsValid() || m_interface->bufferState == nullptr)
    {
        return ResourceState::Undefined;
    }
    return m_interface->bufferState(m_internal, buffer);
}

Result<TextureHandle, Error> Device::CreateTexture(const TextureDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createTexture == nullptr)
    {
        return Result<TextureHandle, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no texture was created"});
    }

    TextureHandle texture;
    const Status<Error> created = m_interface->createTexture(m_internal, desc, &texture);
    if (!created)
    {
        return Result<TextureHandle, Error>::Err(created.Error());
    }
    if (!texture.IsValid())
    {
        return Result<TextureHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid texture handle"});
    }
    return Result<TextureHandle, Error>::Ok(texture);
}

Status<Error> Device::DestroyTexture(TextureHandle texture) noexcept
{
    if (!IsValid() || m_interface->destroyTexture == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyTexture(m_internal, texture);
}

TextureInfo Device::TextureInfoOf(TextureHandle texture) const noexcept
{
    if (!IsValid() || m_interface->textureInfo == nullptr)
    {
        return TextureInfo{};
    }
    return m_interface->textureInfo(m_internal, texture);
}

Result<TextureViewHandle, Error> Device::CreateTextureView(const TextureViewDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createTextureView == nullptr)
    {
        return Result<TextureViewHandle, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no view was created"});
    }

    TextureViewHandle view;
    const Status<Error> created = m_interface->createTextureView(m_internal, desc, &view);
    if (!created)
    {
        return Result<TextureViewHandle, Error>::Err(created.Error());
    }
    if (!view.IsValid())
    {
        return Result<TextureViewHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid view handle"});
    }
    return Result<TextureViewHandle, Error>::Ok(view);
}

Status<Error> Device::DestroyTextureView(TextureViewHandle view) noexcept
{
    if (!IsValid() || m_interface->destroyTextureView == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyTextureView(m_internal, view);
}

TextureViewInfo Device::TextureViewInfoOf(TextureViewHandle view) const noexcept
{
    if (!IsValid() || m_interface->textureViewInfo == nullptr)
    {
        return TextureViewInfo{};
    }
    return m_interface->textureViewInfo(m_internal, view);
}

ResourceState Device::TextureStateOf(TextureHandle texture, u32 mipLevel,
                                     u32 arrayLayer) const noexcept
{
    if (!IsValid() || m_interface->textureState == nullptr)
    {
        return ResourceState::Undefined;
    }
    return m_interface->textureState(m_internal, texture, mipLevel, arrayLayer);
}

Result<SamplerHandle, Error> Device::CreateSampler(const SamplerDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createSampler == nullptr)
    {
        return Result<SamplerHandle, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no sampler was created"});
    }

    SamplerHandle sampler;
    const Status<Error> created = m_interface->createSampler(m_internal, desc, &sampler);
    if (!created)
    {
        return Result<SamplerHandle, Error>::Err(created.Error());
    }
    if (!sampler.IsValid())
    {
        return Result<SamplerHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid sampler handle"});
    }
    return Result<SamplerHandle, Error>::Ok(sampler);
}

Status<Error> Device::DestroySampler(SamplerHandle sampler) noexcept
{
    if (!IsValid() || m_interface->destroySampler == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroySampler(m_internal, sampler);
}

Status<Error> Device::Transition(memory::Span<const BufferBarrier> buffers,
                                 memory::Span<const TextureBarrier> textures) noexcept
{
    if (!IsValid() || m_interface->transition == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->transition(m_internal, buffers, textures);
}

Status<Error> Device::WaitIdle() noexcept
{
    if (!IsValid() || m_interface->waitIdle == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->waitIdle(m_internal);
}

void Device::Release() noexcept
{
    if (m_backend != nullptr && m_internal != nullptr && m_interface != nullptr &&
        m_interface->waitIdle != nullptr)
    {
        (void)m_interface->waitIdle(m_internal);
    }
    if (m_backend != nullptr && m_backend->destroyDevice != nullptr && m_internal != nullptr)
    {
        m_backend->destroyDevice(m_internal);
    }
    m_backend = nullptr;
    m_interface = nullptr;
    m_internal = nullptr;
}

void Device::MoveFrom(Device& other) noexcept
{
    m_backend = other.m_backend;
    m_interface = other.m_interface;
    m_internal = other.m_internal;
    other.m_backend = nullptr;
    other.m_interface = nullptr;
    other.m_internal = nullptr;
}

}  // namespace cyber::rhi
