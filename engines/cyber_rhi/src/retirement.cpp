// Deferred destruction.
//
// Destroying a resource the GPU is still reading is the classic renderer crash:
// it works on one driver because the object happens to survive until the frame
// ends, and corrupts the image on another. The queue exists so a caller can say
// "destroy this when frame N is done" once, at the point it knows the answer, and
// never think about it again.

#include <cyber/rhi/device.h>
#include <cyber/rhi/retirement.h>

namespace cyber::rhi
{

namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::usize;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;

[[nodiscard]] Status<Error> Fail(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

// Destroys one entry and reports whether the device accepted it. A stale handle
// is a caller that already destroyed the object; that is not an error here,
// because a retirement queue must not turn a redundant destroy into a frame
// failure.
[[nodiscard]] bool DestroyEntry(Device& device, const RetireEntry& entry) noexcept
{
    switch (entry.kind)
    {
    case RetireKind::Buffer:
        return device.DestroyBuffer(UnpackHandle<BufferTag>(entry.object)).IsOk();
    case RetireKind::Texture:
        return device.DestroyTexture(UnpackHandle<TextureTag>(entry.object)).IsOk();
    case RetireKind::TextureView:
        return device.DestroyTextureView(UnpackHandle<TextureViewTag>(entry.object)).IsOk();
    case RetireKind::Sampler:
        return device.DestroySampler(UnpackHandle<SamplerTag>(entry.object)).IsOk();
    case RetireKind::Shader:
        return device.DestroyShader(UnpackHandle<ShaderTag>(entry.object)).IsOk();
    case RetireKind::Pipeline:
    {
        const PipelineHandle pipeline = UnpackHandle<PipelineTag>(entry.object);
        const PipelineInfo info = device.PipelineInfoOf(pipeline);
        if (info.valid && info.kind == PipelineKind::Compute)
        {
            return device.DestroyComputePipeline(pipeline).IsOk();
        }
        return device.DestroyGraphicsPipeline(pipeline).IsOk();
    }
    case RetireKind::BindGroup:
        return device.DestroyBindGroup(UnpackHandle<BindGroupTag>(entry.object)).IsOk();
    case RetireKind::BindGroupLayout:
        return device.DestroyBindGroupLayout(UnpackHandle<BindGroupLayoutTag>(entry.object)).IsOk();
    case RetireKind::PipelineLayout:
        return device.DestroyPipelineLayout(UnpackHandle<PipelineLayoutTag>(entry.object)).IsOk();
    case RetireKind::PipelineCache:
        return device.DestroyPipelineCache(UnpackHandle<PipelineCacheTag>(entry.object)).IsOk();
    case RetireKind::Count:
        break;
    }
    return false;
}

}  // namespace

RetirementQueue::RetirementQueue(Allocator& allocator) noexcept : m_entries(allocator) {}

Status<Error> RetirementQueue::Enqueue(RetireKind kind, u64 packed, FenceHandle fence,
                                       u64 value) noexcept
{
    if (packed == 0)
    {
        return Fail(ErrorCode::InvalidArgument, "a retirement entry names no object");
    }
    if (!fence.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "a retirement entry needs a valid fence");
    }

    RetireEntry entry;
    entry.kind = kind;
    entry.object = packed;
    entry.fence = PackHandle(fence);
    entry.value = value;
    if (!m_entries.TryPushBack(entry))
    {
        return Fail(ErrorCode::Internal, "retirement queue could not grow its entry list");
    }
    return Status<Error>::Ok();
}

Status<Error> RetirementQueue::Retire(BufferHandle buffer, FenceHandle fence, u64 value) noexcept
{
    if (!buffer.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale buffer handle");
    }
    return Enqueue(RetireKind::Buffer, PackHandle(buffer), fence, value);
}

Status<Error> RetirementQueue::Retire(TextureHandle texture, FenceHandle fence, u64 value) noexcept
{
    if (!texture.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale texture handle");
    }
    return Enqueue(RetireKind::Texture, PackHandle(texture), fence, value);
}

Status<Error> RetirementQueue::Retire(TextureViewHandle view, FenceHandle fence, u64 value) noexcept
{
    if (!view.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale view handle");
    }
    return Enqueue(RetireKind::TextureView, PackHandle(view), fence, value);
}

Status<Error> RetirementQueue::Retire(SamplerHandle sampler, FenceHandle fence, u64 value) noexcept
{
    if (!sampler.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale sampler handle");
    }
    return Enqueue(RetireKind::Sampler, PackHandle(sampler), fence, value);
}

Status<Error> RetirementQueue::Retire(ShaderHandle shader, FenceHandle fence, u64 value) noexcept
{
    if (!shader.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale shader handle");
    }
    return Enqueue(RetireKind::Shader, PackHandle(shader), fence, value);
}

Status<Error> RetirementQueue::Retire(PipelineHandle pipeline, FenceHandle fence,
                                      u64 value) noexcept
{
    if (!pipeline.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale pipeline handle");
    }
    return Enqueue(RetireKind::Pipeline, PackHandle(pipeline), fence, value);
}

Status<Error> RetirementQueue::Retire(BindGroupHandle group, FenceHandle fence, u64 value) noexcept
{
    if (!group.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale bind group handle");
    }
    return Enqueue(RetireKind::BindGroup, PackHandle(group), fence, value);
}

Status<Error> RetirementQueue::Retire(BindGroupLayoutHandle layout, FenceHandle fence,
                                      u64 value) noexcept
{
    if (!layout.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale layout handle");
    }
    return Enqueue(RetireKind::BindGroupLayout, PackHandle(layout), fence, value);
}

Status<Error> RetirementQueue::Retire(PipelineLayoutHandle layout, FenceHandle fence,
                                      u64 value) noexcept
{
    if (!layout.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale pipeline layout handle");
    }
    return Enqueue(RetireKind::PipelineLayout, PackHandle(layout), fence, value);
}

Status<Error> RetirementQueue::Retire(PipelineCacheHandle cache, FenceHandle fence,
                                      u64 value) noexcept
{
    if (!cache.IsValid())
    {
        return Fail(ErrorCode::InvalidArgument, "retirement names a stale cache handle");
    }
    return Enqueue(RetireKind::PipelineCache, PackHandle(cache), fence, value);
}

Result<u32, Error> RetirementQueue::Collect(Device& device) noexcept
{
    if (!device.IsValid())
    {
        return Result<u32, Error>::Err(
            Error{ErrorCode::InvalidArgument, "retirement collection needs a live device"});
    }

    u32 destroyed = 0;
    usize index = 0;
    while (index < m_entries.Size())
    {
        const RetireEntry entry = m_entries[index];
        const FenceHandle fence = UnpackHandle<FenceTag>(entry.fence);
        if (device.FenceValue(fence) < entry.value)
        {
            ++index;
            continue;
        }
        if (DestroyEntry(device, entry))
        {
            ++destroyed;
        }
        // Swap removal keeps collection linear: the order entries were queued in
        // has no meaning once their fences have passed.
        m_entries[index] = m_entries[m_entries.Size() - 1];
        (void)m_entries.TryResize(m_entries.Size() - 1);
    }
    return Result<u32, Error>::Ok(destroyed);
}

u32 RetirementQueue::Flush(Device& device) noexcept
{
    if (!device.IsValid())
    {
        return 0;
    }
    u32 destroyed = 0;
    for (usize index = 0; index < m_entries.Size(); ++index)
    {
        if (DestroyEntry(device, m_entries[index]))
        {
            ++destroyed;
        }
    }
    m_entries.Clear();
    return destroyed;
}

void RetirementQueue::Clear() noexcept
{
    m_entries.Clear();
}

usize RetirementQueue::Pending() const noexcept
{
    return m_entries.Size();
}

bool RetirementQueue::Empty() const noexcept
{
    return m_entries.Empty();
}

usize RetirementQueue::Capacity() const noexcept
{
    return m_entries.Capacity();
}

}  // namespace cyber::rhi
