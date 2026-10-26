// The command list class and the argument rules every backend shares.
//
// Two kinds of check live here. The class checks that the list is recording,
// because that is its own state and a caller should get the error before a
// backend is entered at all. The free functions check the argument structures
// for the rules no backend may bend — a zero draw count, a NaN viewport, an
// overlapping copy — so a backend only has to add what it alone can see.

#include <cyber/rhi/backend.h>
#include <cyber/rhi/command_list.h>

#include <utility>

namespace cyber::rhi
{

namespace
{

using cyber::core::ErrorCode;
using cyber::core::usize;

[[nodiscard]] Status<Error> Fail(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

// Largest value a 32-bit vertex or index number can hold. A draw whose last
// element would pass it is rejected here rather than inside a driver.
inline constexpr u64 kMaxElementIndex = 0xFFFFFFFFULL;

}  // namespace

// ---------------------------------------------------------------------------
// Argument validation
// ---------------------------------------------------------------------------

Status<Error> ValidateDrawArguments(const DrawArguments& arguments) noexcept
{
    if (arguments.vertexCount == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "a draw needs at least one vertex");
    }
    if (arguments.instanceCount == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "a draw needs at least one instance");
    }
    if (static_cast<u64>(arguments.firstVertex) + arguments.vertexCount > kMaxElementIndex)
    {
        return Fail(ErrorCode::OutOfRange, "a draw's vertex range overflows a 32-bit vertex index");
    }
    return Status<Error>::Ok();
}

Status<Error> ValidateDrawIndexedArguments(const DrawIndexedArguments& arguments) noexcept
{
    if (arguments.indexCount == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "an indexed draw needs at least one index");
    }
    if (arguments.instanceCount == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "an indexed draw needs at least one instance");
    }
    if (static_cast<u64>(arguments.firstIndex) + arguments.indexCount > kMaxElementIndex)
    {
        return Fail(ErrorCode::OutOfRange,
                    "an indexed draw's index range overflows a 32-bit index");
    }
    return Status<Error>::Ok();
}

Status<Error> ValidateDispatchArguments(const DispatchArguments& arguments) noexcept
{
    if (arguments.groupCountX == 0 || arguments.groupCountY == 0 || arguments.groupCountZ == 0)
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a dispatch needs at least one work group on every axis");
    }
    if (arguments.groupCountX > kMaxDispatchGroupsPerAxis ||
        arguments.groupCountY > kMaxDispatchGroupsPerAxis ||
        arguments.groupCountZ > kMaxDispatchGroupsPerAxis)
    {
        return Fail(ErrorCode::OutOfRange,
                    "a dispatch asks for more groups per axis than any backend accepts");
    }
    return Status<Error>::Ok();
}

Status<Error> ValidateViewport(const Viewport& viewport) noexcept
{
    if (!IsFiniteFloat(viewport.x) || !IsFiniteFloat(viewport.y) ||
        !IsFiniteFloat(viewport.width) || !IsFiniteFloat(viewport.height))
    {
        return Fail(ErrorCode::InvalidArgument, "a viewport has a NaN or infinite component");
    }
    if (viewport.width <= 0.0f || viewport.height <= 0.0f)
    {
        return Fail(ErrorCode::InvalidArgument, "a viewport needs a positive extent");
    }
    if (!IsFiniteFloat(viewport.minDepth) || !IsFiniteFloat(viewport.maxDepth))
    {
        return Fail(ErrorCode::InvalidArgument, "a viewport depth range is not finite");
    }
    // The projection already produces reverse-Z, so this is the raw 0..1 window
    // depth range, not a reversed one: minDepth is nearest, maxDepth is farthest.
    if (viewport.minDepth < 0.0f || viewport.maxDepth > 1.0f ||
        viewport.minDepth > viewport.maxDepth)
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a viewport depth range must be inside 0..1 and not inverted");
    }
    return Status<Error>::Ok();
}

Status<Error> ValidateScissor(const ScissorRect& scissor) noexcept
{
    if (scissor.width == 0 || scissor.height == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "a scissor rectangle needs a positive extent");
    }
    if (static_cast<u64>(scissor.x) + scissor.width > kMaxElementIndex ||
        static_cast<u64>(scissor.y) + scissor.height > kMaxElementIndex)
    {
        return Fail(ErrorCode::OutOfRange, "a scissor rectangle overflows a 32-bit extent");
    }
    return Status<Error>::Ok();
}

Status<Error> ValidateBufferCopy(const BufferCopy& copy) noexcept
{
    if (!copy.source.IsValid() || !copy.destination.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "a buffer copy needs both buffers");
    }
    if (copy.sizeBytes == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "a buffer copy needs a non-zero size");
    }
    if (copy.sourceOffset + copy.sizeBytes < copy.sourceOffset ||
        copy.destinationOffset + copy.sizeBytes < copy.destinationOffset)
    {
        return Fail(ErrorCode::OutOfRange, "a buffer copy range overflows a 64-bit offset");
    }
    if (copy.source == copy.destination)
    {
        const u64 sourceEnd = copy.sourceOffset + copy.sizeBytes;
        const u64 destinationEnd = copy.destinationOffset + copy.sizeBytes;
        if (copy.sourceOffset < destinationEnd && copy.destinationOffset < sourceEnd)
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a buffer copy's source and destination ranges overlap");
        }
    }
    return Status<Error>::Ok();
}

Status<Error> ValidateTextureCopy(const TextureCopy& copy) noexcept
{
    if (!copy.source.IsValid() || !copy.destination.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "a texture copy needs both textures");
    }
    if (copy.width == 0 || copy.height == 0 || copy.depth == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "a texture copy needs a non-zero extent");
    }
    return Status<Error>::Ok();
}

Status<Error> ValidateBufferTextureCopy(const BufferTextureCopy& copy, u32 bytesPerRow) noexcept
{
    if (!copy.texture.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "a buffer-texture copy needs a texture");
    }
    if (copy.width == 0 || copy.height == 0 || copy.depth == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "a buffer-texture copy needs a non-zero extent");
    }
    if (copy.bufferRowPitch != 0 && bytesPerRow != 0 && copy.bufferRowPitch < bytesPerRow)
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a buffer-texture copy's row pitch is smaller than one row");
    }
    if (copy.bufferDepthPitch != 0 && copy.bufferRowPitch != 0)
    {
        const u64 rowsBytes = static_cast<u64>(copy.bufferRowPitch) * copy.height;
        if (copy.bufferDepthPitch < rowsBytes)
        {
            return Fail(ErrorCode::InvalidArgument,
                        "a buffer-texture copy's depth pitch is smaller than its rows");
        }
    }
    return Status<Error>::Ok();
}

Status<Error> ValidateIndirectArguments(u64 offsetBytes, u32 count, u32 strideBytes,
                                        u32 minimumStrideBytes) noexcept
{
    if (count == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "an indirect call needs at least one argument");
    }
    if (strideBytes < minimumStrideBytes)
    {
        return Fail(ErrorCode::InvalidArgument,
                    "an indirect stride is smaller than one argument structure");
    }
    if (strideBytes % 4 != 0)
    {
        return Fail(ErrorCode::InvalidArgument, "an indirect stride is not a multiple of four");
    }
    if (offsetBytes % 4 != 0)
    {
        return Fail(ErrorCode::InvalidArgument, "an indirect offset is not a multiple of four");
    }
    if (offsetBytes + static_cast<u64>(strideBytes) * count < offsetBytes)
    {
        return Fail(ErrorCode::OutOfRange, "an indirect argument range overflows");
    }
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// CommandList
// ---------------------------------------------------------------------------

CommandList::~CommandList()
{
    Release();
}

CommandList::CommandList(CommandList&& other) noexcept
{
    MoveFrom(other);
}

CommandList& CommandList::operator=(CommandList&& other) noexcept
{
    if (this != &other)
    {
        Release();
        MoveFrom(other);
    }
    return *this;
}

bool CommandList::IsValid() const noexcept
{
    return m_interface != nullptr && m_internal != nullptr && m_handle.IsValid();
}

CommandListHandle CommandList::Handle() const noexcept
{
    return m_handle;
}

CommandListState CommandList::State() const noexcept
{
    return m_state;
}

bool CommandList::IsRecording() const noexcept
{
    return IsValid() && m_state == CommandListState::Recording;
}

QueueKind CommandList::Queue() const noexcept
{
    return m_queue;
}

CommandListStats CommandList::Stats() const noexcept
{
    if (!IsValid() || m_interface->stats == nullptr)
    {
        return CommandListStats{};
    }
    return m_interface->stats(m_internal);
}

RenderPassInfo CommandList::CurrentRenderPass() const noexcept
{
    if (!IsValid() || m_interface->currentRenderPass == nullptr)
    {
        return RenderPassInfo{};
    }
    return m_interface->currentRenderPass(m_internal);
}

Status<Error> CommandList::Begin() noexcept
{
    if (!IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "command list is empty");
    }
    if (m_state == CommandListState::Recording)
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is already recording; end it before beginning again");
    }
    const Status<Error> begun = m_interface->begin(m_internal);
    if (begun)
    {
        m_state = CommandListState::Recording;
    }
    return begun;
}

Status<Error> CommandList::End() noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; begin it before ending it");
    }
    const Status<Error> ended = m_interface->end(m_internal);
    if (ended)
    {
        m_state = CommandListState::Ended;
    }
    return ended;
}

void CommandList::Reset() noexcept
{
    if (!IsValid())
    {
        return;
    }
    m_interface->reset(m_internal);
    m_state = CommandListState::Initial;
}

Status<Error> CommandList::BeginRenderPass(const RenderPassDesc& desc) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateRenderPassDesc(desc);
    if (!valid)
    {
        return valid;
    }
    return m_interface->beginRenderPass(m_internal, desc);
}

Status<Error> CommandList::EndRenderPass() noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    return m_interface->endRenderPass(m_internal);
}

Status<Error> CommandList::SetPipeline(PipelineHandle pipeline) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    return m_interface->setPipeline(m_internal, pipeline);
}

Status<Error> CommandList::SetBindGroup(BindGroupSlot slot, BindGroupHandle group) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    return m_interface->setBindGroup(m_internal, slot, group);
}

Status<Error> CommandList::SetVertexBuffer(u32 binding, BufferHandle buffer,
                                           u64 offsetBytes) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    return m_interface->setVertexBuffer(m_internal, binding, buffer, offsetBytes);
}

Status<Error> CommandList::SetIndexBuffer(BufferHandle buffer, u64 offsetBytes,
                                          IndexFormat format) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    return m_interface->setIndexBuffer(m_internal, buffer, offsetBytes, format);
}

Status<Error> CommandList::SetViewport(const Viewport& viewport) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateViewport(viewport);
    if (!valid)
    {
        return valid;
    }
    return m_interface->setViewport(m_internal, viewport);
}

Status<Error> CommandList::SetScissor(const ScissorRect& scissor) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateScissor(scissor);
    if (!valid)
    {
        return valid;
    }
    return m_interface->setScissor(m_internal, scissor);
}

Status<Error> CommandList::Draw(const DrawArguments& arguments) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateDrawArguments(arguments);
    if (!valid)
    {
        return valid;
    }
    return m_interface->draw(m_internal, arguments);
}

Status<Error> CommandList::DrawIndexed(const DrawIndexedArguments& arguments) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateDrawIndexedArguments(arguments);
    if (!valid)
    {
        return valid;
    }
    return m_interface->drawIndexed(m_internal, arguments);
}

Status<Error> CommandList::DrawIndirect(BufferHandle arguments, u64 offsetBytes, u32 drawCount,
                                        u32 strideBytes) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid =
        ValidateIndirectArguments(offsetBytes, drawCount, strideBytes, kIndirectDrawStrideBytes);
    if (!valid)
    {
        return valid;
    }
    return m_interface->drawIndirect(m_internal, arguments, offsetBytes, drawCount, strideBytes);
}

Status<Error> CommandList::DrawIndexedIndirect(BufferHandle arguments, u64 offsetBytes,
                                               u32 drawCount, u32 strideBytes) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateIndirectArguments(offsetBytes, drawCount, strideBytes,
                                                          kIndirectDrawIndexedStrideBytes);
    if (!valid)
    {
        return valid;
    }
    return m_interface->drawIndexedIndirect(m_internal, arguments, offsetBytes, drawCount,
                                            strideBytes);
}

Status<Error> CommandList::Dispatch(const DispatchArguments& arguments) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateDispatchArguments(arguments);
    if (!valid)
    {
        return valid;
    }
    return m_interface->dispatch(m_internal, arguments);
}

Status<Error> CommandList::DispatchIndirect(BufferHandle arguments, u64 offsetBytes) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateIndirectArguments(
        offsetBytes, 1, kIndirectDispatchStrideBytes, kIndirectDispatchStrideBytes);
    if (!valid)
    {
        return valid;
    }
    return m_interface->dispatchIndirect(m_internal, arguments, offsetBytes);
}

Status<Error> CommandList::CopyBuffer(const BufferCopy& copy) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateBufferCopy(copy);
    if (!valid)
    {
        return valid;
    }
    return m_interface->copyBuffer(m_internal, copy);
}

Status<Error> CommandList::CopyBufferToTexture(const BufferTextureCopy& copy) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateBufferTextureCopy(copy, 0);
    if (!valid)
    {
        return valid;
    }
    return m_interface->copyBufferToTexture(m_internal, copy);
}

Status<Error> CommandList::CopyTextureToBuffer(const BufferTextureCopy& copy) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateBufferTextureCopy(copy, 0);
    if (!valid)
    {
        return valid;
    }
    return m_interface->copyTextureToBuffer(m_internal, copy);
}

Status<Error> CommandList::CopyTexture(const TextureCopy& copy) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    const Status<Error> valid = ValidateTextureCopy(copy);
    if (!valid)
    {
        return valid;
    }
    return m_interface->copyTexture(m_internal, copy);
}

Status<Error> CommandList::ClearColorTexture(TextureHandle texture, const ClearColorValue& value,
                                             const SubresourceRange& range) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    if (!value.IsFinite())
    {
        return Fail(ErrorCode::InvalidArgument, "a clear value has a NaN or infinite component");
    }
    return m_interface->clearColorTexture(m_internal, texture, value, range);
}

Status<Error> CommandList::ClearDepthStencilTexture(TextureHandle texture,
                                                    const ClearDepthStencilValue& value,
                                                    const SubresourceRange& range) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    if (!value.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "a depth clear value is outside 0..1 or not finite");
    }
    return m_interface->clearDepthStencilTexture(m_internal, texture, value, range);
}

Status<Error> CommandList::Barrier(memory::Span<const BufferBarrier> buffers,
                                   memory::Span<const TextureBarrier> textures) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    return m_interface->barrier(m_internal, buffers, textures);
}

Status<Error> CommandList::PushConstants(ShaderStageMask stages, u32 offsetBytes,
                                         memory::Span<const memory::Byte> data) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    if (data.Data() == nullptr && !data.Empty())
    {
        return Fail(ErrorCode::InvalidArgument, "push constant data is null");
    }
    if (data.Empty())
    {
        return Fail(ErrorCode::InvalidArgument, "push constants need at least one byte");
    }
    if (offsetBytes % 4 != 0 || data.Size() % 4 != 0)
    {
        return Fail(ErrorCode::InvalidArgument,
                    "push constant offset and size must be four byte aligned");
    }
    return m_interface->pushConstants(m_internal, stages, offsetBytes, data);
}

Status<Error> CommandList::BeginDebugLabel(const char* label) noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    if (label == nullptr || label[0] == '\0')
    {
        return Fail(ErrorCode::InvalidArgument, "a debug label needs a non-empty name");
    }
    return m_interface->beginDebugLabel(m_internal, label);
}

Status<Error> CommandList::EndDebugLabel() noexcept
{
    if (!IsRecording())
    {
        return Fail(ErrorCode::InvalidArgument,
                    "the command list is not recording; call Begin() first");
    }
    return m_interface->endDebugLabel(m_internal);
}

void CommandList::Release() noexcept
{
    if (m_device != nullptr && m_deviceInternal != nullptr && m_handle.IsValid() &&
        m_device->destroyCommandList != nullptr)
    {
        (void)m_device->destroyCommandList(m_deviceInternal, m_handle);
    }
    m_device = nullptr;
    m_deviceInternal = nullptr;
    m_interface = nullptr;
    m_internal = nullptr;
    m_handle.Reset();
    m_state = CommandListState::Initial;
    m_queue = QueueKind::Graphics;
}

void CommandList::MoveFrom(CommandList& other) noexcept
{
    m_device = other.m_device;
    m_deviceInternal = other.m_deviceInternal;
    m_interface = other.m_interface;
    m_internal = other.m_internal;
    m_handle = other.m_handle;
    m_state = other.m_state;
    m_queue = other.m_queue;

    other.m_device = nullptr;
    other.m_deviceInternal = nullptr;
    other.m_interface = nullptr;
    other.m_internal = nullptr;
    other.m_handle.Reset();
    other.m_state = CommandListState::Initial;
    other.m_queue = QueueKind::Graphics;
}

}  // namespace cyber::rhi
