#include "d3d12_internal.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include <cyber/rhi/command_list.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/submission.h>

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

// Most lists one submission may hand to a queue. A frame uses a handful; the cap
// lets a batch be gathered on the stack instead of allocating per submit.
inline constexpr u32 kMaxSubmitLists = 32;

// A buffer-texture copy is one footprint, and the API requires its offset and
// every row pitch to be aligned. A caller that packs tightly is told so rather
// than silently reading the wrong bytes.
inline constexpr u32 kTextureRowPitchAlignment = 256;
inline constexpr u64 kTextureBufferOffsetAlignment = 512;

// The device keeps descriptor zero back as scratch, so a clear outside a pass can
// name an attachment it holds no view for.
inline constexpr u32 kScratchDescriptor = 0;

[[nodiscard]] D3D12_COMMAND_LIST_TYPE ToListType(QueueKind queue) noexcept
{
    switch (queue)
    {
    case QueueKind::Graphics:
        return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case QueueKind::Compute:
        return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case QueueKind::Copy:
        return D3D12_COMMAND_LIST_TYPE_COPY;
    case QueueKind::Count:
        break;
    }
    return D3D12_COMMAND_LIST_TYPE_DIRECT;
}

[[nodiscard]] D3D12_PRIMITIVE_TOPOLOGY ToTopology(PrimitiveTopology topology) noexcept
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveTopology::LineList:
        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::LineStrip:
        return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case PrimitiveTopology::TriangleList:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::Count:
        break;
    }
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

[[nodiscard]] DXGI_FORMAT ToIndexFormat(IndexFormat format) noexcept
{
    return format == IndexFormat::Uint16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}

[[nodiscard]] bool HasBufferUsage(BufferUsage usage, BufferUsage bit) noexcept
{
    return (static_cast<u32>(usage) & static_cast<u32>(bit)) != 0;
}

[[nodiscard]] bool HasTextureUsage(TextureUsage usage, TextureUsage bit) noexcept
{
    return (static_cast<u32>(usage) & static_cast<u32>(bit)) != 0;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuViewHandle(D3D12Device& device, u32 index) noexcept
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = device.viewHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * device.viewDescriptorSize;
    return handle;
}

[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuViewHandle(D3D12Device& device, u32 index) noexcept
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = device.viewHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * device.viewDescriptorSize;
    return handle;
}

[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuSamplerHandle(D3D12Device& device, u32 index) noexcept
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = device.samplerHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * device.samplerDescriptorSize;
    return handle;
}

void SetHeaps(D3D12CommandListSlot& list, D3D12Device& device) noexcept
{
    ID3D12DescriptorHeap* heaps[2] = {device.viewHeap, device.samplerHeap};
    list.list->SetDescriptorHeaps(2, heaps);
}

// Mip levels a range addresses, clamped by what the texture has.
[[nodiscard]] u32 RangeMips(const D3D12TextureSlot& texture, const SubresourceRange& range) noexcept
{
    if (range.baseMipLevel >= texture.desc.mipLevels)
    {
        return 0;
    }
    const u32 remaining = texture.desc.mipLevels - range.baseMipLevel;
    return range.mipLevelCount == 0 || range.mipLevelCount > remaining ? remaining
                                                                       : range.mipLevelCount;
}

// Array layers a range addresses, clamped by what the texture has.
[[nodiscard]] u32 RangeLayers(const D3D12TextureSlot& texture,
                              const SubresourceRange& range) noexcept
{
    if (range.baseArrayLayer >= texture.desc.arrayLayers)
    {
        return 0;
    }
    const u32 remaining = texture.desc.arrayLayers - range.baseArrayLayer;
    return range.layerCount == 0 || range.layerCount > remaining ? remaining : range.layerCount;
}

// Collects the transitions one barrier call needs, validating every entry before
// any is applied so a batch with one bad barrier changes nothing.
[[nodiscard]] Status<Error> CollectBarriers(D3D12Device& device,
                                            memory::Span<const BufferBarrier> buffers,
                                            memory::Span<const TextureBarrier> textures,
                                            DynamicArray<D3D12_RESOURCE_BARRIER>* out,
                                            u32* transitionCount) noexcept
{
    for (usize index = 0; index < buffers.Size(); ++index)
    {
        const BufferBarrier& barrier = buffers[index];
        D3D12BufferSlot* buffer = Resolve(device, barrier.buffer);
        if (buffer == nullptr || buffer->resource == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: barrier names a stale buffer");
        }
        // Host-visible and readback buffers live in heaps whose state the API
        // fixes at creation; a transition on one is an error, so the tracked
        // state moves and no barrier is recorded.
        const bool fixedState = HasBufferUsage(buffer->desc.usage, BufferUsage::HostVisible) ||
                                HasBufferUsage(buffer->desc.usage, BufferUsage::Readback);
        const D3D12_RESOURCE_STATES after = ToD3D12State(barrier.to);
        // A transition to the state the barrier's own tracking already holds
        // records nothing: the API rejects a transition whose two states are
        // equal, and a caller asking for the state it is already in is asking
        // for a no-op, not reporting an error.
        if (!fixedState && buffer->native != after)
        {
            D3D12_RESOURCE_BARRIER entry{};
            entry.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            entry.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            entry.Transition.pResource = buffer->resource;
            entry.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            entry.Transition.StateBefore = buffer->native;
            entry.Transition.StateAfter = after;
            if (!out->TryPushBack(entry))
            {
                return StatusError(ErrorCode::Internal,
                                   "d3d12 backend: barrier batch allocation failed");
            }
            buffer->native = after;
        }
        buffer->state = barrier.to;
        ++*transitionCount;
    }

    for (usize index = 0; index < textures.Size(); ++index)
    {
        const TextureBarrier& barrier = textures[index];
        D3D12TextureSlot* texture = Resolve(device, barrier.texture);
        if (texture == nullptr || texture->resource == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: barrier names a stale texture");
        }
        const u32 mips = RangeMips(*texture, barrier.range);
        const u32 layers = RangeLayers(*texture, barrier.range);
        if (mips == 0 || layers == 0)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: barrier range is outside the texture");
        }
        const D3D12_RESOURCE_STATES after = ToD3D12State(barrier.to);
        for (u32 layer = 0; layer < layers; ++layer)
        {
            for (u32 mip = 0; mip < mips; ++mip)
            {
                const u32 mipLevel = barrier.range.baseMipLevel + mip;
                const u32 arrayLayer = barrier.range.baseArrayLayer + layer;
                const u32 subresource = SubresourceIndex(texture->desc, mipLevel, arrayLayer);
                if (subresource >= kMaxTrackedSubresources)
                {
                    return StatusError(
                        ErrorCode::OutOfRange,
                        "d3d12 backend: texture has more subresources than are tracked");
                }
                // As for buffers: a subresource already in the target state needs
                // no barrier, and recording one would hand the API a transition
                // whose two states are equal, which it rejects.
                if (texture->native[subresource] != after)
                {
                    D3D12_RESOURCE_BARRIER entry{};
                    entry.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    entry.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    entry.Transition.pResource = texture->resource;
                    entry.Transition.Subresource = subresource;
                    entry.Transition.StateBefore = texture->native[subresource];
                    entry.Transition.StateAfter = after;
                    if (!out->TryPushBack(entry))
                    {
                        return StatusError(ErrorCode::Internal,
                                           "d3d12 backend: barrier batch allocation failed");
                    }
                    texture->native[subresource] = after;
                }
                texture->states[subresource] = barrier.to;
                ++*transitionCount;
            }
        }
    }
    return Status<Error>::Ok();
}

[[nodiscard]] Status<Error> ValidateFootprint(const BufferTextureCopy& copy,
                                              u32 bytesPerRow) noexcept
{
    if (copy.bufferOffset % kTextureBufferOffsetAlignment != 0)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy buffer offset must be 512-byte aligned");
    }
    if (copy.bufferRowPitch != 0 && copy.bufferRowPitch % kTextureRowPitchAlignment != 0)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy row pitch must be 256-byte aligned");
    }
    if (copy.bufferRowPitch != 0 && bytesPerRow != 0 && copy.bufferRowPitch < bytesPerRow)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy row pitch is smaller than one row");
    }
    return Status<Error>::Ok();
}

[[nodiscard]] u32 TightRowBytes(Format format, u32 width) noexcept
{
    const FormatInfo info = FormatInfoOf(format);
    if (info.bytesPerBlock == 0)
    {
        return 0;
    }
    const u32 texelsPerRow = (width + info.blockWidth - 1) / info.blockWidth;
    return texelsPerRow * info.bytesPerBlock;
}

}  // namespace

D3D12CommandListSlot* Resolve(D3D12Device& device, CommandListHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.commandLists.Size())
    {
        return nullptr;
    }
    D3D12CommandListSlot& slot = device.commandLists[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

namespace
{

// Every recording entry starts here: the list must exist and be open. The device
// is reached through the slot, because the interface only ever receives it.
[[nodiscard]] D3D12CommandListSlot* Recording(void* internal, Error* error) noexcept
{
    if (internal == nullptr)
    {
        *error = Error{ErrorCode::InvalidArgument, "d3d12 backend: command list is null"};
        return nullptr;
    }
    auto* slot = static_cast<D3D12CommandListSlot*>(internal);
    if (slot->state != CommandListState::Recording || slot->list == nullptr)
    {
        *error = Error{ErrorCode::InvalidArgument, "d3d12 backend: command list is not recording"};
        return nullptr;
    }
    return slot;
}

Status<Error> Begin(void* internal) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: command list is null");
    }
    auto& list = *static_cast<D3D12CommandListSlot*>(internal);
    if (list.state == CommandListState::Recording)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: command list is already recording");
    }
    if (list.allocator == nullptr || list.list == nullptr || list.device == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: command list has been destroyed");
    }
    if (FAILED(list.allocator->Reset()))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: command allocator reset failed");
    }
    if (FAILED(list.list->Reset(list.allocator, nullptr)))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: command list reset failed");
    }

    list.state = CommandListState::Recording;
    list.stats = CommandListStats{};
    list.pass = RenderPassInfo{};
    list.passOpen = false;
    list.labelDepth = 0;
    list.pipeline = PipelineHandle{};
    list.pipelineKind = PipelineKind::Graphics;
    list.layout = PipelineLayoutHandle{};
    list.indexBuffer = BufferHandle{};
    list.indexOffset = 0;
    for (u32 slotIndex = 0; slotIndex < kBindGroupSlotCount; ++slotIndex)
    {
        list.groups[slotIndex] = BindGroupHandle{};
    }
    for (u32 binding = 0; binding < kMaxVertexBindings; ++binding)
    {
        list.vertexBuffers[binding] = BufferHandle{};
        list.vertexOffsets[binding] = 0;
        list.vertexBound[binding] = false;
    }

    SetHeaps(list, *list.device);
    return Status<Error>::Ok();
}

Status<Error> End(void* internal) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (list->passOpen)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: a render pass is still open");
    }
    if (list->labelDepth != 0)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: a debug label is still open");
    }
    if (FAILED(list->list->Close()))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: command list close failed");
    }
    list->state = CommandListState::Ended;
    return Status<Error>::Ok();
}

void Reset(void* internal) noexcept
{
    if (internal == nullptr)
    {
        return;
    }
    auto& list = *static_cast<D3D12CommandListSlot*>(internal);
    if (list.list != nullptr && list.state == CommandListState::Recording)
    {
        // A list abandoned mid-recording is closed so its allocator can be reset
        // again, and nothing it recorded is ever submitted.
        (void)list.list->Close();
    }
    list.state = CommandListState::Initial;
    list.passOpen = false;
    list.labelDepth = 0;
    list.stats = CommandListStats{};
}

// ---------------------------------------------------------------------------
// Render passes
// ---------------------------------------------------------------------------

Status<Error> BeginRenderPass(void* internal, const RenderPassDesc& desc) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (list->passOpen)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: a render pass is already open");
    }
    const Status<Error> validated = ValidateRenderPassDesc(desc);
    if (!validated)
    {
        return validated;
    }

    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);

    RenderPassInfo info{};
    info.colorAttachmentCount = static_cast<u32>(desc.colorAttachments.Size());
    D3D12_CPU_DESCRIPTOR_HANDLE targets[kMaxColorAttachments]{};
    u32 width = 0;
    u32 height = 0;
    u32 sampleCount = 0;

    for (u32 index = 0; index < info.colorAttachmentCount; ++index)
    {
        const ColorAttachment& attachment = desc.colorAttachments[index];
        D3D12ViewSlot* view = Resolve(device, attachment.view);
        if (view == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: pass names a stale color view");
        }
        D3D12TextureSlot* texture = Resolve(device, view->texture);
        if (texture == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: pass names a view of a stale texture");
        }
        if (!HasTextureUsage(texture->desc.usage, TextureUsage::RenderTarget))
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: color attachment is not a render target");
        }
        if (index == 0)
        {
            width = texture->desc.width;
            height = texture->desc.height;
            sampleCount = texture->desc.sampleCount;
        }
        else if (texture->desc.width != width || texture->desc.height != height ||
                 texture->desc.sampleCount != sampleCount)
        {
            return StatusError(
                ErrorCode::InvalidArgument,
                "d3d12 backend: pass attachments disagree on extent or sample count");
        }
        // The view was created against this texture's usage, so its descriptor is
        // an RTV; a resolve target is the caller's second pass, not this one.
        targets[index] = DescriptorCpuHandle(device, *view);
        info.colorFormats[index] = view->desc.format;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE depthTarget{};
    bool hasDepth = false;
    if (desc.hasDepthStencil)
    {
        D3D12ViewSlot* view = Resolve(device, desc.depthStencil.view);
        if (view == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: pass names a stale depth view");
        }
        D3D12TextureSlot* texture = Resolve(device, view->texture);
        if (texture == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: pass names a view of a stale texture");
        }
        if (!HasTextureUsage(texture->desc.usage, TextureUsage::DepthStencil))
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: depth attachment is not a depth texture");
        }
        if (info.colorAttachmentCount > 0 &&
            (texture->desc.width != width || texture->desc.height != height ||
             texture->desc.sampleCount != sampleCount))
        {
            return StatusError(
                ErrorCode::InvalidArgument,
                "d3d12 backend: depth attachment disagrees with the color attachments");
        }
        if (info.colorAttachmentCount == 0)
        {
            width = texture->desc.width;
            height = texture->desc.height;
            sampleCount = texture->desc.sampleCount;
        }
        depthTarget = DescriptorCpuHandle(device, *view);
        hasDepth = true;
        info.depthStencilFormat = view->desc.format;
        info.hasDepthStencil = true;
    }

    if (width == 0 || height == 0)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pass has no attachment with an extent");
    }

    list->list->OMSetRenderTargets(info.colorAttachmentCount,
                                   info.colorAttachmentCount > 0 ? targets : nullptr, FALSE,
                                   hasDepth ? &depthTarget : nullptr);

    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                                  0.0f, 1.0f};
    list->list->RSSetViewports(1, &viewport);
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->list->RSSetScissorRects(1, &scissor);

    for (u32 index = 0; index < info.colorAttachmentCount; ++index)
    {
        const ColorAttachment& attachment = desc.colorAttachments[index];
        if (attachment.loadOp != LoadOp::Clear)
        {
            continue;
        }
        const float clear[4] = {attachment.clearValue.red, attachment.clearValue.green,
                                attachment.clearValue.blue, attachment.clearValue.alpha};
        list->list->ClearRenderTargetView(targets[index], clear, 0, nullptr);
        ++list->stats.clears;
    }

    if (hasDepth)
    {
        const DepthStencilAttachment& attachment = desc.depthStencil;
        D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0);
        if (attachment.depthLoadOp == LoadOp::Clear)
        {
            flags |= D3D12_CLEAR_FLAG_DEPTH;
        }
        if (attachment.stencilLoadOp == LoadOp::Clear)
        {
            flags |= D3D12_CLEAR_FLAG_STENCIL;
        }
        if (flags != 0)
        {
            list->list->ClearDepthStencilView(depthTarget, flags, attachment.clearValue.depth,
                                              attachment.clearValue.stencil, 0, nullptr);
            ++list->stats.clears;
        }
    }

    info.valid = true;
    info.width = width;
    info.height = height;
    info.sampleCount = sampleCount == 0 ? 1 : sampleCount;
    list->pass = info;
    list->passOpen = true;
    ++list->stats.renderPasses;
    return Status<Error>::Ok();
}

Status<Error> EndRenderPass(void* internal) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (!list->passOpen)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: no render pass is open");
    }
    // Store and discard need no command in this backend: a flip-model target is
    // written in place, and a discarded attachment is simply never read.
    list->passOpen = false;
    list->pass = RenderPassInfo{};
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

Status<Error> SetPipeline(void* internal, PipelineHandle pipeline) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    auto& device = *list->device;

    D3D12PipelineSlot* slot = nullptr;
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        slot = Resolve(device, pipeline);
    }
    if (slot == nullptr || slot->state == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: pipeline is stale");
    }
    D3D12PipelineLayoutSlot* layout = nullptr;
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        layout = Resolve(device, slot->layout);
    }
    if (layout == nullptr || layout->rootSignature == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: pipeline layout is stale");
    }

    if (slot->kind == PipelineKind::Graphics)
    {
        if (!list->passOpen)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: graphics pipeline outside a render pass");
        }
        if (slot->sampleCount != list->pass.sampleCount)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: pipeline sample count does not match the pass");
        }
        if (slot->colorAttachmentCount != list->pass.colorAttachmentCount ||
            slot->depthStencilFormat != list->pass.depthStencilFormat)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: pipeline attachments do not match the pass");
        }
        for (u32 index = 0; index < slot->colorAttachmentCount; ++index)
        {
            if (slot->colorFormats[index] != list->pass.colorFormats[index])
            {
                return StatusError(ErrorCode::InvalidArgument,
                                   "d3d12 backend: pipeline attachment format differs from pass");
            }
        }
        list->list->SetPipelineState(slot->state);
        list->list->SetGraphicsRootSignature(layout->rootSignature);
        list->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    else
    {
        if (list->passOpen)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: compute pipeline inside a render pass");
        }
        list->list->SetPipelineState(slot->state);
        list->list->SetComputeRootSignature(layout->rootSignature);
    }

    list->pipeline = pipeline;
    list->pipelineKind = slot->kind;
    list->layout = slot->layout;
    ++list->stats.pipelineChanges;
    return Status<Error>::Ok();
}

Status<Error> SetBindGroup(void* internal, BindGroupSlot groupSlot, BindGroupHandle group) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (static_cast<u32>(groupSlot) >= kBindGroupSlotCount)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: bind group slot is invalid");
    }
    if (!list->pipeline.IsValid())
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: a bind group needs a bound pipeline");
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);

    D3D12GroupSlot* slot = Resolve(device, group);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: bind group is stale");
    }
    D3D12PipelineLayoutSlot* layout = Resolve(device, list->layout);
    if (layout == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: pipeline layout is stale");
    }
    const u32 index = static_cast<u32>(groupSlot);
    if (layout->descriptorCounts[index] == 0)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline layout does not use this bind group slot");
    }
    if (layout->resourceParameter[index] != kNoRootParameter && slot->descriptorCount > 0)
    {
        const D3D12_GPU_DESCRIPTOR_HANDLE table = GpuViewHandle(device, slot->descriptorIndex);
        if (list->pipelineKind == PipelineKind::Graphics)
        {
            list->list->SetGraphicsRootDescriptorTable(layout->resourceParameter[index], table);
        }
        else
        {
            list->list->SetComputeRootDescriptorTable(layout->resourceParameter[index], table);
        }
    }
    if (layout->samplerParameter[index] != kNoRootParameter && slot->samplerCount > 0)
    {
        const D3D12_GPU_DESCRIPTOR_HANDLE table = GpuSamplerHandle(device, slot->samplerIndex);
        if (list->pipelineKind == PipelineKind::Graphics)
        {
            list->list->SetGraphicsRootDescriptorTable(layout->samplerParameter[index], table);
        }
        else
        {
            list->list->SetComputeRootDescriptorTable(layout->samplerParameter[index], table);
        }
    }

    list->groups[index] = group;
    ++list->stats.bindGroupChanges;
    return Status<Error>::Ok();
}

Status<Error> SetVertexBuffer(void* internal, u32 binding, BufferHandle buffer,
                              u64 offsetBytes) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (!list->pipeline.IsValid() || list->pipelineKind != PipelineKind::Graphics)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: a vertex buffer needs a graphics pipeline");
    }
    if (binding >= kMaxVertexBindings)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: vertex binding index is out of range");
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12PipelineSlot* pipeline = Resolve(device, list->pipeline);
    if (pipeline == nullptr || binding >= pipeline->vertexBindingCount)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: the pipeline declares no such vertex binding");
    }
    D3D12BufferSlot* slot = Resolve(device, buffer);
    if (slot == nullptr || slot->resource == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: vertex buffer is stale");
    }
    if (!HasBufferUsage(slot->desc.usage, BufferUsage::Vertex))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: buffer was not created for vertex input");
    }
    if (slot->state != ResourceState::VertexBuffer)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: vertex buffer is not in the VertexBuffer state");
    }
    if (offsetBytes >= slot->desc.sizeBytes)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: vertex buffer offset is past the buffer");
    }

    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = slot->resource->GetGPUVirtualAddress() + offsetBytes;
    view.SizeInBytes = static_cast<UINT>(slot->desc.sizeBytes - offsetBytes);
    view.StrideInBytes = pipeline->vertexStrides[binding];
    list->list->IASetVertexBuffers(binding, 1, &view);

    list->vertexBuffers[binding] = buffer;
    list->vertexOffsets[binding] = offsetBytes;
    list->vertexBound[binding] = true;
    return Status<Error>::Ok();
}

Status<Error> SetIndexBuffer(void* internal, BufferHandle buffer, u64 offsetBytes,
                             IndexFormat format) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (!list->pipeline.IsValid() || list->pipelineKind != PipelineKind::Graphics)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: an index buffer needs a graphics pipeline");
    }
    if (format >= IndexFormat::Count)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: index format is invalid");
    }
    const u32 indexBytes = IndexFormatBytes(format);
    if (offsetBytes % indexBytes != 0)
    {
        return StatusError(
            ErrorCode::InvalidArgument,
            "d3d12 backend: index buffer offset is not a multiple of the index size");
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12BufferSlot* slot = Resolve(device, buffer);
    if (slot == nullptr || slot->resource == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: index buffer is stale");
    }
    if (!HasBufferUsage(slot->desc.usage, BufferUsage::Index))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: buffer was not created for index input");
    }
    if (slot->state != ResourceState::IndexBuffer)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: index buffer is not in the IndexBuffer state");
    }
    if (offsetBytes >= slot->desc.sizeBytes)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: index buffer offset is past the buffer");
    }

    D3D12_INDEX_BUFFER_VIEW view{};
    view.BufferLocation = slot->resource->GetGPUVirtualAddress() + offsetBytes;
    view.SizeInBytes = static_cast<UINT>(slot->desc.sizeBytes - offsetBytes);
    view.Format = ToIndexFormat(format);
    list->list->IASetIndexBuffer(&view);

    list->indexBuffer = buffer;
    list->indexOffset = offsetBytes;
    list->indexFormat = format;
    return Status<Error>::Ok();
}

Status<Error> SetViewport(void* internal, const Viewport& viewport) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (!list->passOpen)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: no render pass is open");
    }
    const Status<Error> validated = ValidateViewport(viewport);
    if (!validated)
    {
        return validated;
    }
    if (viewport.x < 0.0f || viewport.y < 0.0f ||
        viewport.x + viewport.width > static_cast<float>(list->pass.width) ||
        viewport.y + viewport.height > static_cast<float>(list->pass.height))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: viewport leaves the render pass extent");
    }
    const D3D12_VIEWPORT target{viewport.x,      viewport.y,        viewport.width,
                                viewport.height, viewport.minDepth, viewport.maxDepth};
    list->list->RSSetViewports(1, &target);
    return Status<Error>::Ok();
}

Status<Error> SetScissor(void* internal, const ScissorRect& scissor) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (!list->passOpen)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: no render pass is open");
    }
    const Status<Error> validated = ValidateScissor(scissor);
    if (!validated)
    {
        return validated;
    }
    if (scissor.x + scissor.width > list->pass.width ||
        scissor.y + scissor.height > list->pass.height)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: scissor leaves the render pass extent");
    }
    const D3D12_RECT rect{static_cast<LONG>(scissor.x), static_cast<LONG>(scissor.y),
                          static_cast<LONG>(scissor.x + scissor.width),
                          static_cast<LONG>(scissor.y + scissor.height)};
    list->list->RSSetScissorRects(1, &rect);
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Draws and dispatches
// ---------------------------------------------------------------------------

Status<Error> ReadyToDraw(D3D12CommandListSlot* list, Error* error) noexcept
{
    if (!list->passOpen)
    {
        *error = Error{ErrorCode::InvalidArgument, "d3d12 backend: no render pass is open"};
        return Status<Error>::Err(*error);
    }
    if (!list->pipeline.IsValid() || list->pipelineKind != PipelineKind::Graphics)
    {
        *error = Error{ErrorCode::InvalidArgument, "d3d12 backend: no graphics pipeline is bound"};
        return Status<Error>::Err(*error);
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12PipelineSlot* pipeline = Resolve(device, list->pipeline);
    if (pipeline == nullptr)
    {
        *error = Error{ErrorCode::InvalidArgument, "d3d12 backend: pipeline is stale"};
        return Status<Error>::Err(*error);
    }
    for (u32 binding = 0; binding < pipeline->vertexBindingCount; ++binding)
    {
        if (!list->vertexBound[binding])
        {
            *error = Error{ErrorCode::InvalidArgument,
                           "d3d12 backend: a vertex binding the pipeline declares is unbound"};
            return Status<Error>::Err(*error);
        }
    }
    D3D12PipelineLayoutSlot* layout = Resolve(device, list->layout);
    if (layout == nullptr)
    {
        *error = Error{ErrorCode::InvalidArgument, "d3d12 backend: pipeline layout is stale"};
        return Status<Error>::Err(*error);
    }
    for (u32 slotIndex = 0; slotIndex < kBindGroupSlotCount; ++slotIndex)
    {
        if (layout->descriptorCounts[slotIndex] > 0 && !list->groups[slotIndex].IsValid())
        {
            *error = Error{ErrorCode::InvalidArgument,
                           "d3d12 backend: a bind group slot the layout uses is unbound"};
            return Status<Error>::Err(*error);
        }
    }
    return Status<Error>::Ok();
}

Status<Error> Draw(void* internal, const DrawArguments& arguments) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    const Status<Error> validated = ValidateDrawArguments(arguments);
    if (!validated)
    {
        return validated;
    }
    const Status<Error> ready = ReadyToDraw(list, &error);
    if (!ready)
    {
        return ready;
    }
    list->list->DrawInstanced(arguments.vertexCount, arguments.instanceCount, arguments.firstVertex,
                              arguments.firstInstance);
    ++list->stats.draws;
    return Status<Error>::Ok();
}

Status<Error> DrawIndexed(void* internal, const DrawIndexedArguments& arguments) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    const Status<Error> validated = ValidateDrawIndexedArguments(arguments);
    if (!validated)
    {
        return validated;
    }
    const Status<Error> ready = ReadyToDraw(list, &error);
    if (!ready)
    {
        return ready;
    }
    if (!list->indexBuffer.IsValid())
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: indexed draw needs an index buffer");
    }
    auto& device = *list->device;
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        D3D12BufferSlot* index = Resolve(device, list->indexBuffer);
        if (index == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: index buffer is stale");
        }
        const u64 required =
            list->indexOffset + static_cast<u64>(arguments.firstIndex + arguments.indexCount) *
                                    IndexFormatBytes(list->indexFormat);
        if (required > index->desc.sizeBytes)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: indexed draw reads past the index buffer");
        }
    }
    list->list->DrawIndexedInstanced(arguments.indexCount, arguments.instanceCount,
                                     arguments.firstIndex, arguments.vertexOffset,
                                     arguments.firstInstance);
    ++list->stats.draws;
    return Status<Error>::Ok();
}

Status<Error> DrawIndirect(void* internal, BufferHandle, u64, u32, u32) noexcept
{
    (void)internal;
    return StatusError(ErrorCode::Unsupported,
                       "d3d12 backend: indirect draws arrive with command signatures");
}

Status<Error> DrawIndexedIndirect(void* internal, BufferHandle, u64, u32, u32) noexcept
{
    (void)internal;
    return StatusError(ErrorCode::Unsupported,
                       "d3d12 backend: indirect draws arrive with command signatures");
}

Status<Error> Dispatch(void* internal, const DispatchArguments& arguments) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    const Status<Error> validated = ValidateDispatchArguments(arguments);
    if (!validated)
    {
        return validated;
    }
    if (list->passOpen)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: dispatch inside a render pass");
    }
    if (!list->pipeline.IsValid() || list->pipelineKind != PipelineKind::Compute)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: dispatch needs a compute pipeline");
    }
    auto& device = *list->device;
    {
        const std::lock_guard<std::mutex> lock(device.mutex);
        D3D12PipelineLayoutSlot* layout = Resolve(device, list->layout);
        if (layout == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: pipeline layout is stale");
        }
        for (u32 slotIndex = 0; slotIndex < kBindGroupSlotCount; ++slotIndex)
        {
            if (layout->descriptorCounts[slotIndex] > 0 && !list->groups[slotIndex].IsValid())
            {
                return StatusError(ErrorCode::InvalidArgument,
                                   "d3d12 backend: a bind group slot the layout uses is unbound");
            }
        }
    }
    list->list->Dispatch(arguments.groupCountX, arguments.groupCountY, arguments.groupCountZ);
    ++list->stats.dispatches;
    return Status<Error>::Ok();
}

Status<Error> DispatchIndirect(void* internal, BufferHandle, u64) noexcept
{
    (void)internal;
    return StatusError(ErrorCode::Unsupported,
                       "d3d12 backend: indirect dispatch arrives with command signatures");
}

// ---------------------------------------------------------------------------
// Copies and clears
// ---------------------------------------------------------------------------

Status<Error> CopyBuffer(void* internal, const BufferCopy& copy) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    const Status<Error> validated = ValidateBufferCopy(copy);
    if (!validated)
    {
        return validated;
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12BufferSlot* source = Resolve(device, copy.source);
    D3D12BufferSlot* destination = Resolve(device, copy.destination);
    if (source == nullptr || destination == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: copy names a stale buffer");
    }
    if (copy.sourceOffset + copy.sizeBytes > source->desc.sizeBytes ||
        copy.destinationOffset + copy.sizeBytes > destination->desc.sizeBytes)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: copy range leaves its buffer");
    }
    if (source->state != ResourceState::CopySource)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: copy source is not in the CopySource state");
    }
    if (destination->state != ResourceState::CopyDestination)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: copy destination is not in the CopyDestination state");
    }
    list->list->CopyBufferRegion(destination->resource, copy.destinationOffset, source->resource,
                                 copy.sourceOffset, copy.sizeBytes);
    ++list->stats.copies;
    return Status<Error>::Ok();
}

Status<Error> CopyBufferToTexture(void* internal, const BufferTextureCopy& copy) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12BufferSlot* buffer = Resolve(device, copy.buffer);
    D3D12TextureSlot* texture = Resolve(device, copy.texture);
    if (buffer == nullptr || texture == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy names a stale resource");
    }
    const Status<Error> validated =
        ValidateBufferTextureCopy(copy, TightRowBytes(texture->desc.format, copy.width));
    if (!validated)
    {
        return validated;
    }
    const Status<Error> footprint =
        ValidateFootprint(copy, TightRowBytes(texture->desc.format, copy.width));
    if (!footprint)
    {
        return footprint;
    }
    if (texture->states[SubresourceIndex(texture->desc, copy.mipLevel, copy.arrayLayer)] !=
        ResourceState::CopyDestination)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy destination is not in CopyDestination");
    }
    if (buffer->state != ResourceState::CopySource)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy source is not in CopySource");
    }

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = texture->resource;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = SubresourceIndex(texture->desc, copy.mipLevel, copy.arrayLayer);

    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = buffer->resource;
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint.Offset = copy.bufferOffset;
    source.PlacedFootprint.Footprint.Format = ToDxgiFormat(texture->desc.format);
    source.PlacedFootprint.Footprint.Width = copy.width;
    source.PlacedFootprint.Footprint.Height = copy.height;
    source.PlacedFootprint.Footprint.Depth = copy.depth;
    source.PlacedFootprint.Footprint.RowPitch =
        copy.bufferRowPitch != 0
            ? copy.bufferRowPitch
            : ((TightRowBytes(texture->desc.format, copy.width) + kTextureRowPitchAlignment - 1) /
               kTextureRowPitchAlignment) *
                  kTextureRowPitchAlignment;

    const D3D12_BOX box{
        copy.x, copy.y, copy.z, copy.x + copy.width, copy.y + copy.height, copy.z + copy.depth};
    list->list->CopyTextureRegion(&destination, copy.x, copy.y, copy.z, &source, &box);
    ++list->stats.copies;
    return Status<Error>::Ok();
}

Status<Error> CopyTextureToBuffer(void* internal, const BufferTextureCopy& copy) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12BufferSlot* buffer = Resolve(device, copy.buffer);
    D3D12TextureSlot* texture = Resolve(device, copy.texture);
    if (buffer == nullptr || texture == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy names a stale resource");
    }
    const Status<Error> validated =
        ValidateBufferTextureCopy(copy, TightRowBytes(texture->desc.format, copy.width));
    if (!validated)
    {
        return validated;
    }
    const Status<Error> footprint =
        ValidateFootprint(copy, TightRowBytes(texture->desc.format, copy.width));
    if (!footprint)
    {
        return footprint;
    }
    if (texture->states[SubresourceIndex(texture->desc, copy.mipLevel, copy.arrayLayer)] !=
        ResourceState::CopySource)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy source is not in CopySource");
    }
    if (buffer->state != ResourceState::CopyDestination)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy destination is not in CopyDestination");
    }

    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = texture->resource;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = SubresourceIndex(texture->desc, copy.mipLevel, copy.arrayLayer);

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = buffer->resource;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Offset = copy.bufferOffset;
    destination.PlacedFootprint.Footprint.Format = ToDxgiFormat(texture->desc.format);
    destination.PlacedFootprint.Footprint.Width = copy.width;
    destination.PlacedFootprint.Footprint.Height = copy.height;
    destination.PlacedFootprint.Footprint.Depth = copy.depth;
    destination.PlacedFootprint.Footprint.RowPitch =
        copy.bufferRowPitch != 0
            ? copy.bufferRowPitch
            : ((TightRowBytes(texture->desc.format, copy.width) + kTextureRowPitchAlignment - 1) /
               kTextureRowPitchAlignment) *
                  kTextureRowPitchAlignment;

    const D3D12_BOX box{
        copy.x, copy.y, copy.z, copy.x + copy.width, copy.y + copy.height, copy.z + copy.depth};
    list->list->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
    ++list->stats.copies;
    return Status<Error>::Ok();
}

Status<Error> CopyTexture(void* internal, const TextureCopy& copy) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    const Status<Error> validated = ValidateTextureCopy(copy);
    if (!validated)
    {
        return validated;
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12TextureSlot* source = Resolve(device, copy.source);
    D3D12TextureSlot* destination = Resolve(device, copy.destination);
    if (source == nullptr || destination == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy names a stale texture");
    }
    if (source->desc.format != destination->desc.format)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy formats differ");
    }
    if (source
            ->states[SubresourceIndex(source->desc, copy.sourceMipLevel, copy.sourceArrayLayer)] !=
        ResourceState::CopySource)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy source is not in CopySource");
    }
    if (destination->states[SubresourceIndex(destination->desc, copy.destinationMipLevel,
                                             copy.destinationArrayLayer)] !=
        ResourceState::CopyDestination)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture copy destination is not in CopyDestination");
    }
    D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
    destinationLocation.pResource = destination->resource;
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destinationLocation.SubresourceIndex =
        SubresourceIndex(destination->desc, copy.destinationMipLevel, copy.destinationArrayLayer);

    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = source->resource;
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sourceLocation.SubresourceIndex =
        SubresourceIndex(source->desc, copy.sourceMipLevel, copy.sourceArrayLayer);

    const D3D12_BOX box{0, 0, 0, copy.width, copy.height, copy.depth};
    list->list->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, &box);
    ++list->stats.copies;
    return Status<Error>::Ok();
}

Status<Error> ClearColorTexture(void* internal, TextureHandle texture, const ClearColorValue& value,
                                const SubresourceRange& range) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (!value.IsFinite())
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: clear value is not finite");
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12TextureSlot* slot = Resolve(device, texture);
    if (slot == nullptr || slot->resource == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: clear names a stale texture");
    }
    if (!HasTextureUsage(slot->desc.usage, TextureUsage::RenderTarget))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture was not created as a render target");
    }
    // A clear has no view to name, so one descriptor is built on the spot in the
    // slot the device keeps back for exactly this.
    D3D12_RENDER_TARGET_VIEW_DESC view{};
    view.Format = ToDxgiFormat(slot->desc.format);
    view.ViewDimension = slot->desc.arrayLayers > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY
                                                    : D3D12_RTV_DIMENSION_TEXTURE2D;
    view.Texture2DArray.MipSlice = range.baseMipLevel;
    view.Texture2DArray.FirstArraySlice = range.baseArrayLayer;
    view.Texture2DArray.ArraySize = range.layerCount == 0 ? 1 : range.layerCount;
    D3D12_CPU_DESCRIPTOR_HANDLE target = device.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    target.ptr += static_cast<SIZE_T>(kScratchDescriptor) * device.rtvDescriptorSize;
    device.device->CreateRenderTargetView(slot->resource, &view, target);

    const float clear[4] = {value.red, value.green, value.blue, value.alpha};
    list->list->ClearRenderTargetView(target, clear, 0, nullptr);
    ++list->stats.clears;
    return Status<Error>::Ok();
}

Status<Error> ClearDepthStencilTexture(void* internal, TextureHandle texture,
                                       const ClearDepthStencilValue& value,
                                       const SubresourceRange& range) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (!value.IsValid())
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: depth clear value is outside 0..1");
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12TextureSlot* slot = Resolve(device, texture);
    if (slot == nullptr || slot->resource == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: clear names a stale texture");
    }
    if (!HasTextureUsage(slot->desc.usage, TextureUsage::DepthStencil))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: texture was not created for depth");
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC view{};
    view.Format = ToDxgiFormat(slot->desc.format);
    view.ViewDimension = slot->desc.arrayLayers > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY
                                                    : D3D12_DSV_DIMENSION_TEXTURE2D;
    view.Texture2DArray.MipSlice = range.baseMipLevel;
    view.Texture2DArray.FirstArraySlice = range.baseArrayLayer;
    view.Texture2DArray.ArraySize = range.layerCount == 0 ? 1 : range.layerCount;
    D3D12_CPU_DESCRIPTOR_HANDLE target = device.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    target.ptr += static_cast<SIZE_T>(kScratchDescriptor) * device.dsvDescriptorSize;
    device.device->CreateDepthStencilView(slot->resource, &view, target);

    D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
    if (IsStencilFormat(slot->desc.format))
    {
        flags |= D3D12_CLEAR_FLAG_STENCIL;
    }
    list->list->ClearDepthStencilView(target, flags, value.depth, value.stencil, 0, nullptr);
    ++list->stats.clears;
    return Status<Error>::Ok();
}

Status<Error> Barrier(void* internal, memory::Span<const BufferBarrier> buffers,
                      memory::Span<const TextureBarrier> textures) noexcept
{
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (buffers.Empty() && textures.Empty())
    {
        return Status<Error>::Ok();
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);

    DynamicArray<D3D12_RESOURCE_BARRIER> batch(*device.allocator);
    u32 transitions = 0;
    const Status<Error> collected =
        CollectBarriers(device, buffers, textures, &batch, &transitions);
    if (!collected)
    {
        return collected;
    }
    // A batch whose every entry was already in its target state collects
    // nothing, and the API counts a call with no barrier as a no-op worth
    // reporting.
    if (!batch.Empty())
    {
        list->list->ResourceBarrier(static_cast<UINT>(batch.Size()), batch.Data());
    }
    list->stats.barriers += transitions;
    return Status<Error>::Ok();
}

Status<Error> PushConstants(void* internal, ShaderStageMask stages, u32 offsetBytes,
                            memory::Span<const memory::Byte> data) noexcept
{
    (void)stages;
    Error error{};
    D3D12CommandListSlot* list = Recording(internal, &error);
    if (list == nullptr)
    {
        return Status<Error>::Err(error);
    }
    if (!list->pipeline.IsValid())
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: push constants need a bound pipeline");
    }
    if (offsetBytes % 4 != 0 || data.Size() % 4 != 0)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: push constants must be four-byte aligned");
    }
    auto& device = *list->device;
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12PipelineLayoutSlot* layout = Resolve(device, list->layout);
    if (layout == nullptr || layout->pushConstantParameter == kNoRootParameter)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: the pipeline layout has no push-constant range");
    }
    if (offsetBytes + data.Size() > layout->pushConstantBytes)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: push constants leave the layout's range");
    }
    // The API counts 32-bit values, and the offset is relative to the root
    // constants rather than to the whole block.
    const u32 offsetValues = offsetBytes / 4;
    const u32 valueCount = static_cast<u32>(data.Size() / 4);
    if (list->pipelineKind == PipelineKind::Graphics)
    {
        list->list->SetGraphicsRoot32BitConstants(layout->pushConstantParameter, valueCount,
                                                  data.Data(), offsetValues);
    }
    else
    {
        list->list->SetComputeRoot32BitConstants(layout->pushConstantParameter, valueCount,
                                                 data.Data(), offsetValues);
    }
    ++list->stats.pushConstants;
    return Status<Error>::Ok();
}

Status<Error> BeginDebugLabel(void* internal, const char*) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: command list is null");
    }
    auto& list = *static_cast<D3D12CommandListSlot*>(internal);
    // Direct3D 12 has no event API of its own: markers come from the PIX event
    // runtime, which is a third-party library this project does not link. The
    // label is counted so a caller can see it was accepted, and nothing pretends
    // to have reached a debugger.
    ++list.labelDepth;
    ++list.stats.debugLabels;
    return Status<Error>::Ok();
}

Status<Error> EndDebugLabel(void* internal) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: command list is null");
    }
    auto& list = *static_cast<D3D12CommandListSlot*>(internal);
    if (list.labelDepth == 0)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: no debug label is open");
    }
    --list.labelDepth;
    return Status<Error>::Ok();
}

RenderPassInfo CurrentRenderPass(const void* internal) noexcept
{
    if (internal == nullptr)
    {
        return RenderPassInfo{};
    }
    const auto& list = *static_cast<const D3D12CommandListSlot*>(internal);
    return list.passOpen ? list.pass : RenderPassInfo{};
}

CommandListStats Stats(const void* internal) noexcept
{
    if (internal == nullptr)
    {
        return CommandListStats{};
    }
    return static_cast<const D3D12CommandListSlot*>(internal)->stats;
}

[[nodiscard]] const CommandListInterface* BuildCommandListInterface() noexcept
{
    static const CommandListInterface interface = []() noexcept
    {
        CommandListInterface value{};
        value.begin = &Begin;
        value.end = &End;
        value.reset = &Reset;
        value.beginRenderPass = &BeginRenderPass;
        value.endRenderPass = &EndRenderPass;
        value.setPipeline = &SetPipeline;
        value.setBindGroup = &SetBindGroup;
        value.setVertexBuffer = &SetVertexBuffer;
        value.setIndexBuffer = &SetIndexBuffer;
        value.setViewport = &SetViewport;
        value.setScissor = &SetScissor;
        value.draw = &Draw;
        value.drawIndexed = &DrawIndexed;
        value.drawIndirect = &DrawIndirect;
        value.drawIndexedIndirect = &DrawIndexedIndirect;
        value.dispatch = &Dispatch;
        value.dispatchIndirect = &DispatchIndirect;
        value.copyBuffer = &CopyBuffer;
        value.copyBufferToTexture = &CopyBufferToTexture;
        value.copyTextureToBuffer = &CopyTextureToBuffer;
        value.copyTexture = &CopyTexture;
        value.clearColorTexture = &ClearColorTexture;
        value.clearDepthStencilTexture = &ClearDepthStencilTexture;
        value.barrier = &Barrier;
        value.pushConstants = &PushConstants;
        value.beginDebugLabel = &BeginDebugLabel;
        value.endDebugLabel = &EndDebugLabel;
        value.currentRenderPass = &CurrentRenderPass;
        value.stats = &Stats;
        return value;
    }();
    return &interface;
}

}  // namespace

// ---------------------------------------------------------------------------
// Creation and release
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateCommandList(void* internal, const CommandListDesc& desc,
                                     CommandListHandle* out, CommandListCreation* creation) noexcept
{
    if (internal == nullptr || out == nullptr || creation == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: list creation needs a device, an output, and a table");
    }
    if (desc.queue >= QueueKind::Count)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: queue class is invalid");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (device.queues[static_cast<u32>(desc.queue)].queue == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: the device has no queue of that class");
    }

    const D3D12_COMMAND_LIST_TYPE type = ToListType(desc.queue);
    ID3D12CommandAllocator* allocator = nullptr;
    if (FAILED(device.device->CreateCommandAllocator(type, __uuidof(ID3D12CommandAllocator),
                                                     reinterpret_cast<void**>(&allocator))) ||
        allocator == nullptr)
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: command allocator creation failed");
    }
    ID3D12GraphicsCommandList* list = nullptr;
    if (FAILED(device.device->CreateCommandList(0, type, allocator, nullptr,
                                                __uuidof(ID3D12GraphicsCommandList),
                                                reinterpret_cast<void**>(&list))) ||
        list == nullptr)
    {
        allocator->Release();
        return StatusError(ErrorCode::Internal, "d3d12 backend: command list creation failed");
    }
    // A list starts open; closing it here makes `begin` the only way to record.
    (void)list->Close();
    NameObject(list, desc.label);

    u32 index = kInvalidHandleIndex;
    if (device.freeCommandListSlots.Size() > 0)
    {
        index = device.freeCommandListSlots[device.freeCommandListSlots.Size() - 1];
        (void)device.freeCommandListSlots.TryResize(device.freeCommandListSlots.Size() - 1);
    }
    else
    {
        if (!device.commandLists.TryPushBack(D3D12CommandListSlot{}))
        {
            list->Release();
            allocator->Release();
            return StatusError(ErrorCode::Internal,
                               "d3d12 backend: command list table growth failed");
        }
        index = static_cast<u32>(device.commandLists.Size() - 1);
    }

    D3D12CommandListSlot& slot = device.commandLists[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.device = &device;
    slot.allocator = allocator;
    slot.list = list;
    slot.queue = desc.queue;
    slot.state = CommandListState::Initial;
    slot.stats = CommandListStats{};
    slot.pass = RenderPassInfo{};
    slot.passOpen = false;
    slot.labelDepth = 0;

    CommandListHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    creation->interface = BuildCommandListInterface();
    creation->internal = &slot;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyCommandList(void* internal, CommandListHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12CommandListSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: command list handle is stale or invalid");
    }
    if (slot->list != nullptr)
    {
        slot->list->Release();
        slot->list = nullptr;
    }
    if (slot->allocator != nullptr)
    {
        slot->allocator->Release();
        slot->allocator = nullptr;
    }
    slot->device = nullptr;
    slot->alive = false;
    slot->state = CommandListState::Initial;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freeCommandListSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal,
                           "d3d12 backend: command list free list growth failed");
    }
    return Status<Error>::Ok();
}

void ReleaseCommandLists(D3D12Device& device) noexcept
{
    for (u32 index = 0; index < device.commandLists.Size(); ++index)
    {
        D3D12CommandListSlot& slot = device.commandLists[index];
        if (slot.list != nullptr)
        {
            slot.list->Release();
            slot.list = nullptr;
        }
        if (slot.allocator != nullptr)
        {
            slot.allocator->Release();
            slot.allocator = nullptr;
        }
        slot.device = nullptr;
        slot.alive = false;
    }
}

// ---------------------------------------------------------------------------
// Submission
// ---------------------------------------------------------------------------

Status<Error> D3D12Submit(void* internal, const SubmitDesc& desc) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    if (desc.queue >= QueueKind::Count)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: queue class is invalid");
    }
    if (desc.commandLists.Empty())
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: submission has no list");
    }
    if (desc.commandLists.Size() > kMaxSubmitLists)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: submission has more lists than one batch allows");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const u32 queueIndex = static_cast<u32>(desc.queue);
    D3D12Queue& queue = device.queues[queueIndex];
    if (queue.queue == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: the device has no queue of that class");
    }

    ID3D12CommandList* lists[kMaxSubmitLists]{};
    u32 barrierCount = 0;
    for (usize index = 0; index < desc.commandLists.Size(); ++index)
    {
        D3D12CommandListSlot* slot = Resolve(device, desc.commandLists[index]);
        if (slot == nullptr || slot->list == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: submission names a stale command list");
        }
        if (slot->state != CommandListState::Ended)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: a submitted command list has not ended");
        }
        if (slot->queue != desc.queue)
        {
            return StatusError(
                ErrorCode::InvalidArgument,
                "d3d12 backend: a submitted command list was recorded for another queue");
        }
        lists[index] = slot->list;
        barrierCount += slot->stats.barriers;
    }

    // Waits first: the queue must not begin before every dependency has passed.
    for (usize index = 0; index < desc.gpuWaits.Size(); ++index)
    {
        const GpuWait& wait = desc.gpuWaits[index];
        D3D12FenceSlot* fence = Resolve(device, wait.fence);
        if (fence == nullptr || fence->fence == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: submission waits on a stale fence");
        }
        if (FAILED(queue.queue->Wait(fence->fence, wait.value)))
        {
            return StatusError(ErrorCode::Internal, "d3d12 backend: queue wait failed");
        }
    }

    queue.queue->ExecuteCommandLists(static_cast<UINT>(desc.commandLists.Size()), lists);

    u64 signalled = 0;
    for (usize index = 0; index < desc.signals.Size(); ++index)
    {
        const FenceSignal& signal = desc.signals[index];
        D3D12FenceSlot* fence = Resolve(device, signal.fence);
        if (fence == nullptr || fence->fence == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: submission signals a stale fence");
        }
        if (FAILED(queue.queue->Signal(fence->fence, signal.value)))
        {
            return StatusError(ErrorCode::Internal, "d3d12 backend: queue signal failed");
        }
        if (signal.value > fence->value)
        {
            fence->value = signal.value;
        }
        if (signal.value > signalled)
        {
            signalled = signal.value;
        }
        if (signal.value > queue.signalled)
        {
            queue.signalled = signal.value;
        }
    }

    ++queue.submissions;
    ++device.submissions;
    queue.last.valid = true;
    queue.last.queue = desc.queue;
    queue.last.commandListCount = static_cast<u32>(desc.commandLists.Size());
    queue.last.barrierCount = barrierCount;
    queue.last.signalledValue = signalled;

    // A signal is what a waiter on this CPU may be blocked on.
    device.condition.notify_all();
    return Status<Error>::Ok();
}

SubmissionInfo D3D12SubmissionInfo(const void* internal, QueueKind kind) noexcept
{
    if (internal == nullptr || kind >= QueueKind::Count)
    {
        return SubmissionInfo{};
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12Queue& queue = device.queues[static_cast<u32>(kind)];
    if (queue.queue == nullptr || queue.submissions == 0)
    {
        return SubmissionInfo{};
    }
    return queue.last;
}

}  // namespace cyber::rhi::detail

#endif
