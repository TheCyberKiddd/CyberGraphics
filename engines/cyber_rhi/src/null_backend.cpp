// The null backend: a real backend that drives no hardware.
//
// It exists so the interface can be exercised, so a headless run and a test can
// use the same code path as a GPU run, and so validation has a home that does not
// depend on a driver. It allocates nothing on a GPU, keeps its state in one
// allocator-backed object per device, and really implements timeline fences so a
// wait that should block does block.

#include <cyber/memory/dynamic_array.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/handle.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>

namespace cyber::rhi
{

namespace
{

using cyber::core::ErrorCode;
using cyber::core::usize;

[[nodiscard]] Status<Error> StatusError(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

// One slot of the fence table. `generation` only increases, and 0 means the slot
// has never been used, so a valid handle always has a non-zero generation.
struct NullFenceSlot
{
    u64 value = 0;
    u32 generation = 0;
    bool alive = false;
};

struct NullDevice
{
    explicit NullDevice(memory::Allocator& allocator) noexcept
        : allocator(&allocator), fences(allocator), freeSlots(allocator)
    {
    }

    memory::Allocator* allocator = nullptr;
    // One lock for the object table and the fence timelines. Coarse on purpose:
    // this backend exists to validate, and Phase 14 measures submission rather
    // than creation.
    mutable std::mutex mutex;
    std::condition_variable condition;
    memory::DynamicArray<NullFenceSlot> fences;
    memory::DynamicArray<u32> freeSlots;
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
    // features a piece of hardware happens to have.
    capabilities.features = Features::Of(Feature::Compute) | Feature::IndirectDraw |
                            Feature::TimelineFences | Feature::DebugMarkers;
    return capabilities;
}

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
