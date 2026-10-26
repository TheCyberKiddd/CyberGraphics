#include "d3d12_device.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include "d3d12_com.h"

#include <cyber/core/error.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/handle.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>

namespace cyber::rhi::detail
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::DynamicArray;
using cyber::memory::kInvalidHandleIndex;

[[nodiscard]] Status<Error> StatusError(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

// Next generation for a slot, skipping zero so a valid handle is always
// distinguishable from a default one.
[[nodiscard]] u32 NextGeneration(u32 current) noexcept
{
    const u32 next = current + 1;
    return next == 0 ? 1 : next;
}

// One timeline fence. The D3D12 object is owned here and released when the slot
// dies, so nothing outside this file holds a fence reference.
struct D3D12FenceSlot
{
    ID3D12Fence* fence = nullptr;
    u64 value = 0;
    bool alive = false;
    u32 generation = 0;
};

// One queue per class. `signalled` is the highest value this queue has ever
// signalled, which is what `SubmissionInfo` reports until submission lands.
struct D3D12Queue
{
    ID3D12CommandQueue* queue = nullptr;
    D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    u64 signalled = 0;
    u64 submissions = 0;
};

struct D3D12Device
{
    explicit D3D12Device(memory::Allocator& allocator) noexcept
        : fences(allocator), freeFenceSlots(allocator)
    {
    }

    D3D12Device(const D3D12Device&) = delete;
    D3D12Device& operator=(const D3D12Device&) = delete;

    // `internal` is the D3D12 device; everything else hangs off it.
    ID3D12Device* device = nullptr;
    // The debug layer, when it was enabled. Released with the device.
    ID3D12Debug* debug = nullptr;
    // Feature level the device was created at. Reported through capabilities as
    // a limit, not through `AdapterInfo::apiVersion`.
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    // The adapter the device was created on, for `Capabilities::adapter`.
    AdapterInfo adapterInfo{};
    // One per queue class, in `QueueKind` order. A class the queue creation
    // failed for stays null and reports an invalid handle.
    D3D12Queue queues[kQueueKindCount]{};
    QueueHandle queueHandles[kQueueKindCount]{};
    Capabilities capabilities{};
    DynamicArray<D3D12FenceSlot> fences;
    DynamicArray<u32> freeFenceSlots;
    memory::Allocator* allocator = nullptr;
    u64 fenceSignals = 0;
    // The device's table is shared by every device of this backend, so a
    // per-device mutex is the only state two devices would share.
    mutable std::mutex mutex;
    std::condition_variable condition;
};

[[nodiscard]] D3D12FenceSlot* ResolveFence(D3D12Device& device, FenceHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.fences.Size())
    {
        return nullptr;
    }
    D3D12FenceSlot& slot = device.fences[handle.index];
    if (!slot.alive || slot.generation != handle.generation)
    {
        return nullptr;
    }
    return &slot;
}

[[nodiscard]] const D3D12FenceSlot* ResolveFence(const D3D12Device& device,
                                                 FenceHandle handle) noexcept
{
    return ResolveFence(const_cast<D3D12Device&>(device), handle);
}

// The limits every D3D12 device guarantees. These are the API's own guaranteed
// maxima, not what this particular adapter reports, because a caller plans
// against the worst case the interface promises.
[[nodiscard]] Limits D3D12Limits() noexcept
{
    Limits limits{};
    // The SDK spells out the 2D and cube bounds but not the 1D and 3D ones, so
    // those two are the feature-level 11_0 guarantees from the D3D12 spec.
    limits.maxTexture1D = 16384;
    limits.maxTexture2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    limits.maxTexture3D = 2048;
    limits.maxTextureCube = D3D12_REQ_TEXTURECUBE_DIMENSION;
    limits.maxTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
    limits.maxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    limits.maxVertexBuffers = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
    limits.maxVertexAttributes = D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT;
    // Four, because the RHI fixes four binding slots by change frequency.
    limits.maxBindGroups = kBindGroupSlotCount;
    limits.maxBindingsPerGroup = 64;
    // D3D12 root constants are 256 shader-visible DWORDs, but the RHI keeps the
    // 128-byte floor that Vulkan guarantees so a layout is portable.
    limits.maxPushConstantBytes = 128;
    limits.maxSamplerAnisotropy = D3D12_MAX_MAXANISOTROPY;
    limits.minUniformBufferOffsetAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    limits.minStorageBufferOffsetAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    // One resource is bounded by the 4 GiB addressable range of a raw buffer,
    // less the placement alignment the API reserves.
    limits.maxBufferBytes = (u64{1} << 32) - D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    return limits;
}

// What this increment can actually honour. Advertising `Compute` before
// `CreateComputePipeline` exists would be a claim a caller could plan against
// and be refused by, so the set grows one increment at a time.
[[nodiscard]] Features D3D12Features(const D3D12Device& device) noexcept
{
    Features features{};
    // Fences are `ID3D12Fence` timelines: a 64-bit value, signalled and waited on
    // by the CPU and by a queue.
    features.Add(Feature::TimelineFences);

    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (device.device != nullptr && SUCCEEDED(device.device->CheckFeatureSupport(
                                        D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
        // Root signatures can point at a resource array without a descriptor per
        // index, which is what a bindless binding needs later.
        if (options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3)
        {
            features.Add(Feature::StorageTextures);
        }
    }
    return features;
}

[[nodiscard]] Capabilities BuildCapabilities(const D3D12Device& device) noexcept
{
    Capabilities capabilities{};
    capabilities.adapter = device.adapterInfo;
    capabilities.limits = D3D12Limits();
    capabilities.features = D3D12Features(device);
    return capabilities;
}

void ReleaseQueues(D3D12Device& device) noexcept
{
    for (u32 index = 0; index < kQueueKindCount; ++index)
    {
        D3D12Queue& queue = device.queues[index];
        if (queue.queue != nullptr)
        {
            queue.queue->Release();
            queue.queue = nullptr;
        }
        queue.signalled = 0;
        queue.submissions = 0;
        device.queueHandles[index].Reset();
    }
}

void ReleaseFences(D3D12Device& device) noexcept
{
    for (u32 index = 0; index < device.fences.Size(); ++index)
    {
        D3D12FenceSlot& slot = device.fences[index];
        if (slot.fence != nullptr)
        {
            // Wake anything still waiting before the object dies.
            (void)slot.fence->Signal(UINT64_MAX);
            slot.fence->Release();
            slot.fence = nullptr;
        }
        slot.alive = false;
        slot.value = 0;
    }
}

// ---------------------------------------------------------------------------
// Fences
// ---------------------------------------------------------------------------

Capabilities D3D12Capabilities(const void* internal) noexcept
{
    if (internal == nullptr)
    {
        return Capabilities{};
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    return device.capabilities;
}

QueueHandle D3D12QueueFor(const void* internal, QueueKind kind) noexcept
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
    const auto& device = *static_cast<const D3D12Device*>(internal);
    return device.queueHandles[index];
}

Status<Error> D3D12CreateFence(void* internal, const FenceDesc& desc, FenceHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: fence creation needs a device and an output handle");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    ID3D12Fence* fence = nullptr;
    const HRESULT result =
        device.device->CreateFence(desc.initialValue, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                   reinterpret_cast<void**>(&fence));
    if (FAILED(result) || fence == nullptr)
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: fence creation failed");
    }
    if (desc.label != nullptr && device.device != nullptr)
    {
        // Object names are what PIX and the debug layer print.
        const int wideLength = MultiByteToWideChar(CP_UTF8, 0, desc.label, -1, nullptr, 0);
        if (wideLength > 1)
        {
            // The name lives as long as the fence, so a fixed buffer is enough.
            wchar_t wide[128]{};
            const int copied = MultiByteToWideChar(CP_UTF8, 0, desc.label, -1, wide,
                                                   wideLength < 128 ? wideLength : 128);
            if (copied > 0)
            {
                (void)fence->SetName(wide);
            }
        }
    }

    u32 index = kInvalidHandleIndex;
    if (device.freeFenceSlots.Size() > 0)
    {
        index = device.freeFenceSlots[device.freeFenceSlots.Size() - 1];
        (void)device.freeFenceSlots.TryResize(device.freeFenceSlots.Size() - 1);
    }
    else
    {
        if (!device.fences.TryPushBack(D3D12FenceSlot{}))
        {
            fence->Release();
            return StatusError(ErrorCode::Internal, "d3d12 backend: fence table growth failed");
        }
        index = static_cast<u32>(device.fences.Size() - 1);
    }

    D3D12FenceSlot& slot = device.fences[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.value = desc.initialValue;
    slot.fence = fence;

    FenceHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyFence(void* internal, FenceHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    ID3D12Fence* doomed = nullptr;
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        D3D12FenceSlot* slot = ResolveFence(device, handle);
        if (slot == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: fence handle is stale or invalid");
        }
        doomed = slot->fence;
        // Keep the COM object alive across the wake-up below: a waiter holds its
        // own reference, and this one covers the release that follows.
        if (doomed != nullptr)
        {
            doomed->AddRef();
        }
        slot->fence = nullptr;
        slot->alive = false;
        slot->generation = NextGeneration(slot->generation);
        slot->value = 0;
        if (!device.freeFenceSlots.TryPushBack(handle.index))
        {
            if (doomed != nullptr)
            {
                doomed->Release();
            }
            return StatusError(ErrorCode::Internal, "d3d12 backend: fence free list growth failed");
        }
    }
    // A waiter must wake and report the handle as stale rather than block until
    // its timeout. Signalling the maximum value completes every pending wait.
    if (doomed != nullptr)
    {
        (void)doomed->Signal(UINT64_MAX);
        doomed->Release();
    }
    device.condition.notify_all();
    return Status<Error>::Ok();
}

Status<Error> D3D12SignalFence(void* internal, FenceHandle handle, u64 value) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    ID3D12CommandQueue* queue = nullptr;
    ID3D12Fence* fence = nullptr;
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        D3D12FenceSlot* slot = ResolveFence(device, handle);
        if (slot == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: fence handle is stale or invalid");
        }
        if (value <= slot->value)
        {
            // A timeline never moves backwards; a signal at or below the current
            // value is already satisfied.
            return Status<Error>::Ok();
        }
        slot->value = value;
        ++device.fenceSignals;
        fence = slot->fence;
        // A queue signal is the faithful mapping: the value is reached when the
        // GPU reaches it, which is what a frame's completion means. Without a
        // graphics queue the CPU signal is the only timeline left.
        queue = device.queues[static_cast<u32>(QueueKind::Graphics)].queue;
        if (queue != nullptr)
        {
            queue->AddRef();
        }
        if (fence != nullptr)
        {
            fence->AddRef();
        }
    }

    if (queue != nullptr)
    {
        const HRESULT result = queue->Signal(fence, value);
        queue->Release();
        fence->Release();
        if (FAILED(result))
        {
            return StatusError(ErrorCode::Internal, "d3d12 backend: queue signal failed");
        }
    }
    else
    {
        (void)fence->Signal(value);
        fence->Release();
    }
    device.condition.notify_all();
    return Status<Error>::Ok();
}

Status<Error> D3D12WaitFence(void* internal, FenceHandle handle, u64 value,
                             u64 timeoutNanoseconds) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    ID3D12Fence* fence = nullptr;
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        D3D12FenceSlot* slot = ResolveFence(device, handle);
        if (slot == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: fence handle is stale or invalid");
        }
        if (slot->fence->GetCompletedValue() >= value)
        {
            return Status<Error>::Ok();
        }
        if (timeoutNanoseconds == 0)
        {
            return StatusError(ErrorCode::NotFound,
                               "d3d12 backend: fence has not reached the value");
        }
        // The wait outlives the lock, so it holds its own reference: a destroy on
        // another thread releases the table's reference, not this one.
        fence = slot->fence;
        fence->AddRef();
    }

    // The event is what makes this a real wait rather than a spin: D3D12 signals
    // it when the GPU reaches the value, and a destroy signals the fence to its
    // maximum so the waiter wakes and reports a stale handle.
    HANDLE event = CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (event == nullptr)
    {
        fence->Release();
        return StatusError(ErrorCode::Internal, "d3d12 backend: wait event creation failed");
    }
    (void)fence->SetEventOnCompletion(value, event);

    DWORD milliseconds = INFINITE;
    if (timeoutNanoseconds != kInfiniteWaitNanoseconds)
    {
        // A bounded wait rounds up, so a sub-millisecond timeout still waits
        // once rather than reporting a spurious timeout.
        const u64 rounded = (timeoutNanoseconds + 999'999ULL) / 1'000'000ULL;
        milliseconds = static_cast<DWORD>(rounded == 0 ? 1 : rounded);
    }
    (void)WaitForSingleObject(event, milliseconds);
    (void)CloseHandle(event);

    Status<Error> result = Status<Error>::Ok();
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        if (ResolveFence(device, handle) == nullptr)
        {
            result = StatusError(ErrorCode::InvalidArgument,
                                 "d3d12 backend: fence was destroyed while waiting");
        }
        else if (fence->GetCompletedValue() < value)
        {
            result = StatusError(ErrorCode::NotFound,
                                 "d3d12 backend: fence timed out before reaching the value");
        }
    }
    fence->Release();
    return result;
}

u64 D3D12FenceValue(const void* internal, FenceHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return 0;
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12FenceSlot* slot = ResolveFence(device, handle);
    return slot != nullptr ? slot->value : 0;
}

Status<Error> D3D12WaitIdle(void* internal) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);

    // Drain every queue that has accepted work by signalling a value on it and
    // waiting for the GPU to reach it. A queue that never submitted returns
    // immediately, which is why no fence is created here.
    for (u32 index = 0; index < kQueueKindCount; ++index)
    {
        D3D12Queue& entry = device.queues[index];
        if (entry.queue == nullptr || entry.submissions == 0)
        {
            continue;
        }
        ID3D12Fence* fence = nullptr;
        const HRESULT created = device.device->CreateFence(
            0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence));
        if (FAILED(created) || fence == nullptr)
        {
            return StatusError(ErrorCode::Internal, "d3d12 backend: idle fence creation failed");
        }
        const u64 value = entry.signalled + 1;
        if (SUCCEEDED(entry.queue->Signal(fence, value)))
        {
            entry.signalled = value;
            HANDLE event = CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
            if (event != nullptr)
            {
                (void)fence->SetEventOnCompletion(value, event);
                (void)WaitForSingleObject(event, INFINITE);
                (void)CloseHandle(event);
            }
        }
        fence->Release();
    }
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Entries this increment does not implement
//
// The table must be complete or `Device::Create` refuses it, so each entry that
// arrives in a later Phase 14 increment answers with `Unsupported` and a message
// that names the increment. A query returns a zeroed value: asking a device what
// it has when it has nothing is not an error.
// ---------------------------------------------------------------------------

constexpr const char* kLaterIncrement = "d3d12 backend: this call arrives in a later Phase 14 "
                                        "increment";

[[nodiscard]] Status<Error> NotYet() noexcept
{
    return Status<Error>::Err(Error{ErrorCode::Unsupported, kLaterIncrement});
}

[[nodiscard]] Status<Error> NoSuchHandle() noexcept
{
    // A destroy of something this backend never created is a stale handle, not an
    // unsupported feature.
    return Status<Error>::Err(
        Error{ErrorCode::InvalidArgument, "d3d12 backend: handle is stale or invalid"});
}

Status<Error> D3D12CreateBuffer(void*, const BufferDesc&, BufferHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyBuffer(void*, BufferHandle) noexcept
{
    return NoSuchHandle();
}
BufferInfo D3D12BufferInfo(const void*, BufferHandle) noexcept
{
    return BufferInfo{};
}
Status<Error> D3D12WriteBuffer(void*, BufferHandle, u64,
                               cyber::memory::Span<const cyber::memory::Byte>) noexcept
{
    return NotYet();
}
Status<Error> D3D12ReadBuffer(void*, BufferHandle, u64,
                              cyber::memory::Span<cyber::memory::Byte>) noexcept
{
    return NotYet();
}
ResourceState D3D12BufferState(const void*, BufferHandle) noexcept
{
    return ResourceState::Undefined;
}
Status<Error> D3D12CreateTexture(void*, const TextureDesc&, TextureHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyTexture(void*, TextureHandle) noexcept
{
    return NoSuchHandle();
}
TextureInfo D3D12TextureInfo(const void*, TextureHandle) noexcept
{
    return TextureInfo{};
}
Status<Error> D3D12CreateTextureView(void*, const TextureViewDesc&, TextureViewHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyTextureView(void*, TextureViewHandle) noexcept
{
    return NoSuchHandle();
}
TextureViewInfo D3D12TextureViewInfo(const void*, TextureViewHandle) noexcept
{
    return TextureViewInfo{};
}
ResourceState D3D12TextureState(const void*, TextureHandle, u32, u32) noexcept
{
    return ResourceState::Undefined;
}
Status<Error> D3D12CreateSampler(void*, const SamplerDesc&, SamplerHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroySampler(void*, SamplerHandle) noexcept
{
    return NoSuchHandle();
}
Status<Error> D3D12Transition(void*, cyber::memory::Span<const BufferBarrier>,
                              cyber::memory::Span<const TextureBarrier>) noexcept
{
    return NotYet();
}
Status<Error> D3D12CreateShader(void*, const ShaderDesc&, ShaderHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyShader(void*, ShaderHandle) noexcept
{
    return NoSuchHandle();
}
ShaderInfo D3D12ShaderInfo(const void*, ShaderHandle) noexcept
{
    return ShaderInfo{};
}
Status<Error> D3D12CreateBindGroupLayout(void*, const BindGroupLayoutDesc&,
                                         BindGroupLayoutHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyBindGroupLayout(void*, BindGroupLayoutHandle) noexcept
{
    return NoSuchHandle();
}
Status<Error> D3D12CreateBindGroup(void*, const BindGroupDesc&, BindGroupHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyBindGroup(void*, BindGroupHandle) noexcept
{
    return NoSuchHandle();
}
BindGroupInfo D3D12BindGroupInfo(const void*, BindGroupHandle) noexcept
{
    return BindGroupInfo{};
}
Status<Error> D3D12CreatePipelineLayout(void*, const PipelineLayoutDesc&,
                                        PipelineLayoutHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyPipelineLayout(void*, PipelineLayoutHandle) noexcept
{
    return NoSuchHandle();
}
Status<Error> D3D12CreateGraphicsPipeline(void*, const GraphicsPipelineDesc&,
                                          PipelineHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyGraphicsPipeline(void*, PipelineHandle) noexcept
{
    return NoSuchHandle();
}
Status<Error> D3D12CreateComputePipeline(void*, const ComputePipelineDesc&,
                                         PipelineHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyComputePipeline(void*, PipelineHandle) noexcept
{
    return NoSuchHandle();
}
Status<Error> D3D12CreatePipelineCache(void*, const PipelineCacheDesc&,
                                       PipelineCacheHandle*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyPipelineCache(void*, PipelineCacheHandle) noexcept
{
    return NoSuchHandle();
}
Status<Error> D3D12PipelineCacheBytes(const void*, PipelineCacheHandle, cyber::memory::Allocator&,
                                      DynamicArray<cyber::memory::Byte>*) noexcept
{
    return NotYet();
}
PipelineInfo D3D12PipelineInfo(const void*, PipelineHandle) noexcept
{
    return PipelineInfo{};
}
Status<Error> D3D12CreateCommandList(void*, const CommandListDesc&, CommandListHandle*,
                                     CommandListCreation*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroyCommandList(void*, CommandListHandle) noexcept
{
    return NoSuchHandle();
}
Status<Error> D3D12Submit(void*, const SubmitDesc&) noexcept
{
    return NotYet();
}
SubmissionInfo D3D12SubmissionInfo(const void*, QueueKind) noexcept
{
    return SubmissionInfo{};
}
Status<Error> D3D12CreateSwapChain(void*, const SwapChainDesc&, SwapChainHandle*,
                                   SwapChainCreation*) noexcept
{
    return NotYet();
}
Status<Error> D3D12DestroySwapChain(void*, SwapChainHandle) noexcept
{
    return NoSuchHandle();
}

[[nodiscard]] const DeviceInterface* BuildInterface() noexcept
{
    static const DeviceInterface interface = []() noexcept
    {
        DeviceInterface value{};
        value.capabilities = &D3D12Capabilities;
        value.queue = &D3D12QueueFor;
        value.createFence = &D3D12CreateFence;
        value.destroyFence = &D3D12DestroyFence;
        value.signalFence = &D3D12SignalFence;
        value.waitFence = &D3D12WaitFence;
        value.fenceValue = &D3D12FenceValue;
        value.waitIdle = &D3D12WaitIdle;

        value.createBuffer = &D3D12CreateBuffer;
        value.destroyBuffer = &D3D12DestroyBuffer;
        value.bufferInfo = &D3D12BufferInfo;
        value.writeBuffer = &D3D12WriteBuffer;
        value.readBuffer = &D3D12ReadBuffer;
        value.bufferState = &D3D12BufferState;
        value.createTexture = &D3D12CreateTexture;
        value.destroyTexture = &D3D12DestroyTexture;
        value.textureInfo = &D3D12TextureInfo;
        value.createTextureView = &D3D12CreateTextureView;
        value.destroyTextureView = &D3D12DestroyTextureView;
        value.textureViewInfo = &D3D12TextureViewInfo;
        value.textureState = &D3D12TextureState;
        value.createSampler = &D3D12CreateSampler;
        value.destroySampler = &D3D12DestroySampler;
        value.transition = &D3D12Transition;

        value.createShader = &D3D12CreateShader;
        value.destroyShader = &D3D12DestroyShader;
        value.shaderInfo = &D3D12ShaderInfo;
        value.createBindGroupLayout = &D3D12CreateBindGroupLayout;
        value.destroyBindGroupLayout = &D3D12DestroyBindGroupLayout;
        value.createBindGroup = &D3D12CreateBindGroup;
        value.destroyBindGroup = &D3D12DestroyBindGroup;
        value.bindGroupInfo = &D3D12BindGroupInfo;
        value.createPipelineLayout = &D3D12CreatePipelineLayout;
        value.destroyPipelineLayout = &D3D12DestroyPipelineLayout;
        value.createGraphicsPipeline = &D3D12CreateGraphicsPipeline;
        value.destroyGraphicsPipeline = &D3D12DestroyGraphicsPipeline;
        value.createComputePipeline = &D3D12CreateComputePipeline;
        value.destroyComputePipeline = &D3D12DestroyComputePipeline;
        value.createPipelineCache = &D3D12CreatePipelineCache;
        value.destroyPipelineCache = &D3D12DestroyPipelineCache;
        value.pipelineCacheBytes = &D3D12PipelineCacheBytes;
        value.pipelineInfo = &D3D12PipelineInfo;

        value.createCommandList = &D3D12CreateCommandList;
        value.destroyCommandList = &D3D12DestroyCommandList;
        value.submit = &D3D12Submit;
        value.submissionInfo = &D3D12SubmissionInfo;
        value.createSwapChain = &D3D12CreateSwapChain;
        value.destroySwapChain = &D3D12DestroySwapChain;
        return value;
    }();
    return &interface;
}

// Finds the adapter at `index` in the order enumeration reports: hardware in
// DXGI's preference order, then WARP. Returns false when the index is past the
// end.
[[nodiscard]] bool FindAdapter(IDXGIFactory4& factory, u32 index,
                               ComOwner<IDXGIAdapter1>& out) noexcept
{
    u32 seen = 0;
    for (u32 slot = 0; slot < kMaxHardwareAdapters; ++slot)
    {
        IDXGIAdapter1* raw = nullptr;
        if (factory.EnumAdapters1(slot, &raw) == DXGI_ERROR_NOT_FOUND || raw == nullptr)
        {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        const bool isSoftware =
            SUCCEEDED(raw->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        if (!isSoftware)
        {
            if (seen == index)
            {
                out.Attach(raw);
                return true;
            }
            ++seen;
        }
        raw->Release();
    }

    IDXGIAdapter1* warp = nullptr;
    if (FAILED(factory.EnumWarpAdapter(__uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&warp))) ||
        warp == nullptr)
    {
        return false;
    }
    if (seen == index)
    {
        out.Attach(warp);
        return true;
    }
    warp->Release();
    return false;
}

}  // namespace

const DeviceInterface* D3D12Interface() noexcept
{
    return BuildInterface();
}

Result<DeviceCreation, Error> CreateD3D12Device(const DeviceDesc& desc,
                                                memory::Allocator& allocator) noexcept
{
    if (desc.backend == nullptr)
    {
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::InvalidArgument, "d3d12 backend: device description has no backend"});
    }

    ComOwner<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), factory.PutVoid())))
    {
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::Internal, "d3d12 backend: DXGI factory creation failed"});
    }

    ComOwner<IDXGIAdapter1> adapter;
    if (!FindAdapter(*factory.Get(), desc.adapterIndex, adapter))
    {
        return Result<DeviceCreation, Error>::Err(Error{
            ErrorCode::OutOfRange, "d3d12 backend: adapter index is outside the adapter list"});
    }

    // The debug layer is enabled before the device exists, because the device
    // records every message from its creation onward. A debug build asks for it
    // unless the caller opted out.
    ComOwner<ID3D12Debug> debug;
#if defined(CG_DEBUG) && CG_DEBUG
    if (desc.validation != ValidationLevel::Off)
    {
        if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), debug.PutVoid())))
        {
            debug->EnableDebugLayer();

            // GPU-based validation catches what the CPU-side checks cannot. It is
            // slow and only available with the debug layer.
            ComOwner<ID3D12Debug3> debug3;
            if (SUCCEEDED(debug.Get()->QueryInterface(__uuidof(ID3D12Debug3), debug3.PutVoid())))
            {
                debug3->SetEnableGPUBasedValidation(TRUE);
            }
        }
        else
        {
            debug.Reset();
        }
    }
#endif

    ID3D12Device* raw = nullptr;
    const HRESULT created =
        D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                          reinterpret_cast<void**>(&raw));
    if (created == E_NOINTERFACE)
    {
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::Unsupported,
                  "d3d12 backend: adapter does not support Direct3D 12 feature level 11_0"});
    }
    if (FAILED(created) || raw == nullptr)
    {
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::Internal, "d3d12 backend: device creation failed"});
    }

    void* memory = allocator.Allocate(sizeof(D3D12Device), alignof(D3D12Device));
    if (memory == nullptr)
    {
        raw->Release();
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::Internal, "d3d12 backend: device bookkeeping allocation failed"});
    }
    auto* device = new (memory) D3D12Device(allocator);
    device->device = raw;
    device->debug = debug.Get();
    if (device->debug != nullptr)
    {
        // Ownership moves to the device; the local owner must not release it.
        device->debug->AddRef();
    }
    device->allocator = &allocator;
    device->adapterInfo = DescribeAdapter(*adapter.Get());

    // A feature level query is what tells a caller which shader model the device
    // accepts; the value is part of the device's identity, not the adapter's.
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
    static const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,
                                                  D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                                  D3D_FEATURE_LEVEL_11_0};
    levels.NumFeatureLevels = static_cast<UINT>(sizeof(requested) / sizeof(requested[0]));
    levels.pFeatureLevelsRequested = requested;
    if (SUCCEEDED(raw->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels))))
    {
        device->featureLevel = levels.MaxSupportedFeatureLevel;
    }

    const D3D12_COMMAND_LIST_TYPE types[kQueueKindCount] = {
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_TYPE_COMPUTE,
        D3D12_COMMAND_LIST_TYPE_COPY,
    };
    for (u32 index = 0; index < kQueueKindCount; ++index)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = types[index];
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        ID3D12CommandQueue* queue = nullptr;
        const HRESULT queued = raw->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                                                       reinterpret_cast<void**>(&queue));
        if (FAILED(queued) || queue == nullptr)
        {
            // A device without its graphics queue cannot do anything the RHI
            // promises, so a partial queue set is a failed creation.
            ReleaseQueues(*device);
            ReleaseFences(*device);
            if (device->debug != nullptr)
            {
                device->debug->Release();
            }
            raw->Release();
            device->~D3D12Device();
            allocator.Deallocate(memory);
            return Result<DeviceCreation, Error>::Err(
                Error{ErrorCode::Internal, "d3d12 backend: command queue creation failed"});
        }
        device->queues[index].queue = queue;
        device->queues[index].type = types[index];
        device->queueHandles[index].index = index;
        device->queueHandles[index].generation = 1;
    }

    device->capabilities = BuildCapabilities(*device);
    debug.Reset();

    DeviceCreation creation{};
    creation.interface = D3D12Interface();
    creation.internal = device;
    return Result<DeviceCreation, Error>::Ok(creation);
}

void DestroyD3D12Device(void* internal) noexcept
{
    if (internal == nullptr)
    {
        return;
    }
    auto* device = static_cast<D3D12Device*>(internal);
    (void)D3D12WaitIdle(device);

    ReleaseFences(*device);
    ReleaseQueues(*device);
    device->fences.Clear();
    device->freeFenceSlots.Clear();
    if (device->debug != nullptr)
    {
        device->debug->Release();
        device->debug = nullptr;
    }
    if (device->device != nullptr)
    {
        device->device->Release();
        device->device = nullptr;
    }
    memory::Allocator* allocator = device->allocator;
    device->~D3D12Device();
    if (allocator != nullptr)
    {
        allocator->Deallocate(device);
    }
}

}  // namespace cyber::rhi::detail

#endif
