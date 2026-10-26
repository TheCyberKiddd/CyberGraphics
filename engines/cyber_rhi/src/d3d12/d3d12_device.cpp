#include "d3d12_device.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include "d3d12_internal.h"

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

// The limits every D3D12 device guarantees: the API's own maxima, not what this
// particular adapter reports, because a caller plans against what the interface
// promises.
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
    limits.maxBindGroups = kBindGroupSlotCount;
    limits.maxBindingsPerGroup = 64;
    // D3D12 root constants reach 256 DWORDs, but the RHI keeps the 128-byte floor
    // Vulkan guarantees so a layout stays portable.
    limits.maxPushConstantBytes = 128;
    limits.maxSamplerAnisotropy = D3D12_MAX_MAXANISOTROPY;
    limits.minUniformBufferOffsetAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    limits.minStorageBufferOffsetAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    limits.maxBufferBytes = (u64{1} << 32) - D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    return limits;
}

// What this increment can honour. The set grows one increment at a time: a
// feature a caller could plan against and then be refused by is worse than an
// absent one.
[[nodiscard]] Features D3D12Features(const D3D12Device& device) noexcept
{
    Features features{};
    features.Add(Feature::TimelineFences);
    features.Add(Feature::AnisotropicFiltering);
    features.Add(Feature::DebugMarkers);

    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (device.device != nullptr && SUCCEEDED(device.device->CheckFeatureSupport(
                                        D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
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
    if (FAILED(device.device->CreateFence(desc.initialValue, D3D12_FENCE_FLAG_NONE,
                                          __uuidof(ID3D12Fence),
                                          reinterpret_cast<void**>(&fence))) ||
        fence == nullptr)
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: fence creation failed");
    }
    NameObject(fence, desc.label);

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
        D3D12FenceSlot* slot = Resolve(device, handle);
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
        D3D12FenceSlot* slot = Resolve(device, handle);
        if (slot == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: fence handle is stale or invalid");
        }
        if (value <= slot->value)
        {
            return Status<Error>::Ok();
        }
        slot->value = value;
        ++device.fenceSignals;
        fence = slot->fence;
        // A queue signal is the faithful mapping: the value is reached when the
        // GPU reaches it, which is what a frame's completion means.
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
        const HRESULT signalled = queue->Signal(fence, value);
        queue->Release();
        fence->Release();
        if (FAILED(signalled))
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
        D3D12FenceSlot* slot = Resolve(device, handle);
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
        // A bounded wait rounds up, so a sub-millisecond timeout still waits once
        // rather than reporting a spurious timeout.
        const u64 rounded = (timeoutNanoseconds + 999'999ULL) / 1'000'000ULL;
        milliseconds = static_cast<DWORD>(rounded == 0 ? 1 : rounded);
    }
    (void)WaitForSingleObject(event, milliseconds);
    (void)CloseHandle(event);

    Status<Error> result = Status<Error>::Ok();
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        if (Resolve(device, handle) == nullptr)
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
    const D3D12FenceSlot* slot = Resolve(const_cast<D3D12Device&>(device), handle);
    return slot != nullptr ? slot->value : 0;
}

Status<Error> D3D12WaitIdle(void* internal) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);

    // Drain every queue that accepted work by signalling a value and waiting for
    // the GPU to reach it. A queue that never submitted returns immediately.
    for (u32 index = 0; index < kQueueKindCount; ++index)
    {
        D3D12Queue& entry = device.queues[index];
        if (entry.queue == nullptr || entry.submissions == 0)
        {
            continue;
        }
        ID3D12Fence* fence = nullptr;
        if (FAILED(device.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                              reinterpret_cast<void**>(&fence))) ||
            fence == nullptr)
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
// arrives in a later Phase 14 increment answers `Unsupported` with a message that
// names the increment. A query returns a zeroed value: asking a device what it
// has when it has nothing is not an error.
// ---------------------------------------------------------------------------

constexpr const char* kLaterIncrement = "d3d12 backend: this call arrives in a later Phase 14 "
                                        "increment";

[[nodiscard]] Status<Error> NotYet() noexcept
{
    return Status<Error>::Err(Error{ErrorCode::Unsupported, kLaterIncrement});
}

[[nodiscard]] Status<Error> NoSuchHandle() noexcept
{
    return Status<Error>::Err(
        Error{ErrorCode::InvalidArgument, "d3d12 backend: handle is stale or invalid"});
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
Status<Error> D3D12PipelineCacheBytes(const void*, PipelineCacheHandle, memory::Allocator&,
                                      DynamicArray<memory::Byte>*) noexcept
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
// DXGI's preference order, then WARP.
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

// One shader-visible heap for views and one for samplers. Both are sized from the
// API's per-heap limit so a device never has to grow one mid-frame.
[[nodiscard]] bool CreateDescriptorHeaps(D3D12Device& device) noexcept
{
    D3D12_DESCRIPTOR_HEAP_DESC viewDesc{};
    viewDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    viewDesc.NumDescriptors = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;
    viewDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device.device->CreateDescriptorHeap(&viewDesc, __uuidof(ID3D12DescriptorHeap),
                                                   reinterpret_cast<void**>(&device.viewHeap))) ||
        device.viewHeap == nullptr)
    {
        return false;
    }
    device.viewDescriptorSize =
        device.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC samplerDesc{};
    samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerDesc.NumDescriptors = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
    samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(
            device.device->CreateDescriptorHeap(&samplerDesc, __uuidof(ID3D12DescriptorHeap),
                                                reinterpret_cast<void**>(&device.samplerHeap))) ||
        device.samplerHeap == nullptr)
    {
        return false;
    }
    device.samplerDescriptorSize =
        device.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    return true;
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
    // records every message from its creation onward.
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
        if (FAILED(raw->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                                           reinterpret_cast<void**>(&queue))) ||
            queue == nullptr)
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

    if (!CreateDescriptorHeaps(*device))
    {
        ReleaseQueues(*device);
        ReleaseFences(*device);
        if (device->viewHeap != nullptr)
        {
            device->viewHeap->Release();
        }
        if (device->samplerHeap != nullptr)
        {
            device->samplerHeap->Release();
        }
        if (device->debug != nullptr)
        {
            device->debug->Release();
        }
        raw->Release();
        device->~D3D12Device();
        allocator.Deallocate(memory);
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::Internal, "d3d12 backend: descriptor heap creation failed"});
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

    ReleaseResources(*device);
    ReleaseFences(*device);
    ReleaseQueues(*device);
    device->fences.Clear();
    device->freeFenceSlots.Clear();
    device->buffers.Clear();
    device->freeBufferSlots.Clear();
    device->textures.Clear();
    device->freeTextureSlots.Clear();
    device->views.Clear();
    device->freeViewSlots.Clear();
    device->samplers.Clear();
    device->freeSamplerSlots.Clear();
    if (device->viewHeap != nullptr)
    {
        device->viewHeap->Release();
        device->viewHeap = nullptr;
    }
    if (device->samplerHeap != nullptr)
    {
        device->samplerHeap->Release();
        device->samplerHeap = nullptr;
    }
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
