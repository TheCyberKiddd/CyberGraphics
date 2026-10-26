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
           interface.destroySampler != nullptr && interface.transition != nullptr &&
           interface.createShader != nullptr && interface.destroyShader != nullptr &&
           interface.shaderInfo != nullptr && interface.createBindGroupLayout != nullptr &&
           interface.destroyBindGroupLayout != nullptr && interface.createBindGroup != nullptr &&
           interface.destroyBindGroup != nullptr && interface.bindGroupInfo != nullptr &&
           interface.createPipelineLayout != nullptr &&
           interface.destroyPipelineLayout != nullptr &&
           interface.createGraphicsPipeline != nullptr &&
           interface.destroyGraphicsPipeline != nullptr &&
           interface.createComputePipeline != nullptr &&
           interface.destroyComputePipeline != nullptr &&
           interface.createPipelineCache != nullptr && interface.destroyPipelineCache != nullptr &&
           interface.pipelineCacheBytes != nullptr && interface.pipelineInfo != nullptr &&
           interface.createCommandList != nullptr && interface.destroyCommandList != nullptr &&
           interface.submit != nullptr && interface.submissionInfo != nullptr &&
           interface.createSwapChain != nullptr && interface.destroySwapChain != nullptr;
}

// A list table is checked once, at creation, for the same reason the device table
// is: every recording call would otherwise have to test its own entry again.
[[nodiscard]] bool CommandListInterfaceIsComplete(const CommandListInterface& interface) noexcept
{
    return interface.begin != nullptr && interface.end != nullptr && interface.reset != nullptr &&
           interface.beginRenderPass != nullptr && interface.endRenderPass != nullptr &&
           interface.setPipeline != nullptr && interface.setBindGroup != nullptr &&
           interface.setVertexBuffer != nullptr && interface.setIndexBuffer != nullptr &&
           interface.setViewport != nullptr && interface.setScissor != nullptr &&
           interface.draw != nullptr && interface.drawIndexed != nullptr &&
           interface.drawIndirect != nullptr && interface.drawIndexedIndirect != nullptr &&
           interface.dispatch != nullptr && interface.dispatchIndirect != nullptr &&
           interface.copyBuffer != nullptr && interface.copyBufferToTexture != nullptr &&
           interface.copyTextureToBuffer != nullptr && interface.copyTexture != nullptr &&
           interface.clearColorTexture != nullptr &&
           interface.clearDepthStencilTexture != nullptr && interface.barrier != nullptr &&
           interface.pushConstants != nullptr && interface.beginDebugLabel != nullptr &&
           interface.endDebugLabel != nullptr && interface.currentRenderPass != nullptr &&
           interface.stats != nullptr;
}

[[nodiscard]] bool SwapChainInterfaceIsComplete(const SwapChainInterface& interface) noexcept
{
    return interface.acquireNextImage != nullptr && interface.present != nullptr &&
           interface.resize != nullptr && interface.info != nullptr;
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

Result<ShaderHandle, Error> Device::CreateShader(const ShaderDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createShader == nullptr)
    {
        return Result<ShaderHandle, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no shader was created"});
    }

    ShaderHandle shader;
    const Status<Error> created = m_interface->createShader(m_internal, desc, &shader);
    if (!created)
    {
        return Result<ShaderHandle, Error>::Err(created.Error());
    }
    if (!shader.IsValid())
    {
        return Result<ShaderHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid shader handle"});
    }
    return Result<ShaderHandle, Error>::Ok(shader);
}

Status<Error> Device::DestroyShader(ShaderHandle shader) noexcept
{
    if (!IsValid() || m_interface->destroyShader == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyShader(m_internal, shader);
}

ShaderInfo Device::ShaderInfoOf(ShaderHandle shader) const noexcept
{
    if (!IsValid() || m_interface->shaderInfo == nullptr)
    {
        return ShaderInfo{};
    }
    return m_interface->shaderInfo(m_internal, shader);
}

Result<BindGroupLayoutHandle, Error>
Device::CreateBindGroupLayout(const BindGroupLayoutDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createBindGroupLayout == nullptr)
    {
        return Result<BindGroupLayoutHandle, Error>::Err(Error{
            core::ErrorCode::InvalidArgument, "device is empty; no bind group layout was created"});
    }

    BindGroupLayoutHandle layout;
    const Status<Error> created = m_interface->createBindGroupLayout(m_internal, desc, &layout);
    if (!created)
    {
        return Result<BindGroupLayoutHandle, Error>::Err(created.Error());
    }
    if (!layout.IsValid())
    {
        return Result<BindGroupLayoutHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid layout handle"});
    }
    return Result<BindGroupLayoutHandle, Error>::Ok(layout);
}

Status<Error> Device::DestroyBindGroupLayout(BindGroupLayoutHandle layout) noexcept
{
    if (!IsValid() || m_interface->destroyBindGroupLayout == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyBindGroupLayout(m_internal, layout);
}

Result<BindGroupHandle, Error> Device::CreateBindGroup(const BindGroupDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createBindGroup == nullptr)
    {
        return Result<BindGroupHandle, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no bind group was created"});
    }

    BindGroupHandle group;
    const Status<Error> created = m_interface->createBindGroup(m_internal, desc, &group);
    if (!created)
    {
        return Result<BindGroupHandle, Error>::Err(created.Error());
    }
    if (!group.IsValid())
    {
        return Result<BindGroupHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid group handle"});
    }
    return Result<BindGroupHandle, Error>::Ok(group);
}

Status<Error> Device::DestroyBindGroup(BindGroupHandle group) noexcept
{
    if (!IsValid() || m_interface->destroyBindGroup == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyBindGroup(m_internal, group);
}

BindGroupInfo Device::BindGroupInfoOf(BindGroupHandle group) const noexcept
{
    if (!IsValid() || m_interface->bindGroupInfo == nullptr)
    {
        return BindGroupInfo{};
    }
    return m_interface->bindGroupInfo(m_internal, group);
}

Result<PipelineLayoutHandle, Error>
Device::CreatePipelineLayout(const PipelineLayoutDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createPipelineLayout == nullptr)
    {
        return Result<PipelineLayoutHandle, Error>::Err(Error{
            core::ErrorCode::InvalidArgument, "device is empty; no pipeline layout was created"});
    }

    PipelineLayoutHandle layout;
    const Status<Error> created = m_interface->createPipelineLayout(m_internal, desc, &layout);
    if (!created)
    {
        return Result<PipelineLayoutHandle, Error>::Err(created.Error());
    }
    if (!layout.IsValid())
    {
        return Result<PipelineLayoutHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid pipeline layout handle"});
    }
    return Result<PipelineLayoutHandle, Error>::Ok(layout);
}

Status<Error> Device::DestroyPipelineLayout(PipelineLayoutHandle layout) noexcept
{
    if (!IsValid() || m_interface->destroyPipelineLayout == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyPipelineLayout(m_internal, layout);
}

Result<PipelineHandle, Error>
Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createGraphicsPipeline == nullptr)
    {
        return Result<PipelineHandle, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no pipeline was created"});
    }

    PipelineHandle pipeline;
    const Status<Error> created = m_interface->createGraphicsPipeline(m_internal, desc, &pipeline);
    if (!created)
    {
        return Result<PipelineHandle, Error>::Err(created.Error());
    }
    if (!pipeline.IsValid())
    {
        return Result<PipelineHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid pipeline handle"});
    }
    return Result<PipelineHandle, Error>::Ok(pipeline);
}

Status<Error> Device::DestroyGraphicsPipeline(PipelineHandle pipeline) noexcept
{
    if (!IsValid() || m_interface->destroyGraphicsPipeline == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyGraphicsPipeline(m_internal, pipeline);
}

Result<PipelineHandle, Error>
Device::CreateComputePipeline(const ComputePipelineDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createComputePipeline == nullptr)
    {
        return Result<PipelineHandle, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no pipeline was created"});
    }

    PipelineHandle pipeline;
    const Status<Error> created = m_interface->createComputePipeline(m_internal, desc, &pipeline);
    if (!created)
    {
        return Result<PipelineHandle, Error>::Err(created.Error());
    }
    if (!pipeline.IsValid())
    {
        return Result<PipelineHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid pipeline handle"});
    }
    return Result<PipelineHandle, Error>::Ok(pipeline);
}

Status<Error> Device::DestroyComputePipeline(PipelineHandle pipeline) noexcept
{
    if (!IsValid() || m_interface->destroyComputePipeline == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyComputePipeline(m_internal, pipeline);
}

Result<PipelineCacheHandle, Error>
Device::CreatePipelineCache(const PipelineCacheDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createPipelineCache == nullptr)
    {
        return Result<PipelineCacheHandle, Error>::Err(Error{
            core::ErrorCode::InvalidArgument, "device is empty; no pipeline cache was created"});
    }

    PipelineCacheHandle cache;
    const Status<Error> created = m_interface->createPipelineCache(m_internal, desc, &cache);
    if (!created)
    {
        return Result<PipelineCacheHandle, Error>::Err(created.Error());
    }
    if (!cache.IsValid())
    {
        return Result<PipelineCacheHandle, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an invalid cache handle"});
    }
    return Result<PipelineCacheHandle, Error>::Ok(cache);
}

Status<Error> Device::DestroyPipelineCache(PipelineCacheHandle cache) noexcept
{
    if (!IsValid() || m_interface->destroyPipelineCache == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    return m_interface->destroyPipelineCache(m_internal, cache);
}

Result<memory::DynamicArray<memory::Byte>, Error>
Device::PipelineCacheBytes(PipelineCacheHandle cache, memory::Allocator& allocator) const noexcept
{
    if (!IsValid() || m_interface->pipelineCacheBytes == nullptr)
    {
        return Result<memory::DynamicArray<memory::Byte>, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty"});
    }

    memory::DynamicArray<memory::Byte> bytes(allocator);
    const Status<Error> read =
        m_interface->pipelineCacheBytes(m_internal, cache, allocator, &bytes);
    if (!read)
    {
        return Result<memory::DynamicArray<memory::Byte>, Error>::Err(read.Error());
    }
    return Result<memory::DynamicArray<memory::Byte>, Error>::Ok(std::move(bytes));
}

PipelineInfo Device::PipelineInfoOf(PipelineHandle pipeline) const noexcept
{
    if (!IsValid() || m_interface->pipelineInfo == nullptr)
    {
        return PipelineInfo{};
    }
    return m_interface->pipelineInfo(m_internal, pipeline);
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

Result<CommandList, Error> Device::CreateCommandList(const CommandListDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createCommandList == nullptr)
    {
        return Result<CommandList, Error>::Err(Error{
            core::ErrorCode::InvalidArgument, "device is empty; no command list was created"});
    }
    if (static_cast<u32>(desc.queue) >= kQueueKindCount)
    {
        return Result<CommandList, Error>::Err(Error{
            core::ErrorCode::InvalidArgument, "command list names a queue class that is not one"});
    }

    CommandListHandle handle;
    CommandListCreation creation;
    const Status<Error> created =
        m_interface->createCommandList(m_internal, desc, &handle, &creation);
    if (!created)
    {
        return Result<CommandList, Error>::Err(created.Error());
    }
    if (!handle.IsValid() || creation.interface == nullptr || creation.internal == nullptr)
    {
        (void)m_interface->destroyCommandList(m_internal, handle);
        return Result<CommandList, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an incomplete command list"});
    }
    if (!CommandListInterfaceIsComplete(*creation.interface))
    {
        (void)m_interface->destroyCommandList(m_internal, handle);
        return Result<CommandList, Error>::Err(Error{
            core::ErrorCode::Internal, "backend returned a command list with an incomplete table"});
    }

    CommandList list;
    list.m_device = m_interface;
    list.m_deviceInternal = m_internal;
    list.m_interface = creation.interface;
    list.m_internal = creation.internal;
    list.m_handle = handle;
    list.m_state = CommandListState::Initial;
    list.m_queue = desc.queue;
    return Result<CommandList, Error>::Ok(std::move(list));
}

Status<Error> Device::Submit(const SubmitDesc& desc) noexcept
{
    if (!IsValid() || m_interface->submit == nullptr)
    {
        return StatusError(core::ErrorCode::InvalidArgument, "device is empty");
    }
    const Status<Error> valid = ValidateSubmitDesc(desc);
    if (!valid)
    {
        return valid;
    }
    return m_interface->submit(m_internal, desc);
}

SubmissionInfo Device::LastSubmission(QueueKind kind) const noexcept
{
    if (!IsValid() || m_interface->submissionInfo == nullptr)
    {
        return SubmissionInfo{};
    }
    return m_interface->submissionInfo(m_internal, kind);
}

Result<SwapChain, Error> Device::CreateSwapChain(const SwapChainDesc& desc) noexcept
{
    if (!IsValid() || m_interface->createSwapChain == nullptr)
    {
        return Result<SwapChain, Error>::Err(
            Error{core::ErrorCode::InvalidArgument, "device is empty; no swapchain was created"});
    }
    const Status<Error> valid = ValidateSwapChainDesc(desc);
    if (!valid)
    {
        return Result<SwapChain, Error>::Err(valid.Error());
    }

    SwapChainHandle handle;
    SwapChainCreation creation;
    const Status<Error> created =
        m_interface->createSwapChain(m_internal, desc, &handle, &creation);
    if (!created)
    {
        return Result<SwapChain, Error>::Err(created.Error());
    }
    if (!handle.IsValid() || creation.interface == nullptr || creation.internal == nullptr)
    {
        (void)m_interface->destroySwapChain(m_internal, handle);
        return Result<SwapChain, Error>::Err(
            Error{core::ErrorCode::Internal, "backend returned an incomplete swapchain"});
    }
    if (!SwapChainInterfaceIsComplete(*creation.interface))
    {
        (void)m_interface->destroySwapChain(m_internal, handle);
        return Result<SwapChain, Error>::Err(Error{
            core::ErrorCode::Internal, "backend returned a swapchain with an incomplete table"});
    }

    SwapChain swapchain;
    swapchain.m_device = m_interface;
    swapchain.m_deviceInternal = m_internal;
    swapchain.m_interface = creation.interface;
    swapchain.m_internal = creation.internal;
    swapchain.m_handle = handle;
    return Result<SwapChain, Error>::Ok(std::move(swapchain));
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
