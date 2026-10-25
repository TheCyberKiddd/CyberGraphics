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
