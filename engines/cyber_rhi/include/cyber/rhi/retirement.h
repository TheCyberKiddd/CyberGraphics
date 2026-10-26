#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/rhi/handle.h>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;

class Device;

// Kind of object a retirement entry names. The values are stable because they
// are stored, not just switched on.
enum class RetireKind : u8
{
    Buffer = 0,
    Texture,
    TextureView,
    Sampler,
    Shader,
    Pipeline,
    BindGroup,
    BindGroupLayout,
    PipelineLayout,
    PipelineCache,
    // Count of enumerators, for table tests. Not a kind.
    Count
};

// Number of real kinds, not counting `Count`.
inline constexpr u32 kRetireKindCount = static_cast<u32>(RetireKind::Count);

// Human-readable kind, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* RetireKindName(RetireKind kind) noexcept
{
    switch (kind)
    {
    case RetireKind::Buffer:
        return "buffer";
    case RetireKind::Texture:
        return "texture";
    case RetireKind::TextureView:
        return "texture_view";
    case RetireKind::Sampler:
        return "sampler";
    case RetireKind::Shader:
        return "shader";
    case RetireKind::Pipeline:
        return "pipeline";
    case RetireKind::BindGroup:
        return "bind_group";
    case RetireKind::BindGroupLayout:
        return "bind_group_layout";
    case RetireKind::PipelineLayout:
        return "pipeline_layout";
    case RetireKind::PipelineCache:
        return "pipeline_cache";
    case RetireKind::Count:
        break;
    }
    return "unknown";
}

// One object waiting for the GPU to finish with it.
//
// The handle is stored packed rather than as a typed handle because the queue is
// homogeneous: it holds whatever a frame used, and the driver has no idea what
// that is. The packing is the same `PackHandle`/`UnpackHandle` pair the rest of
// the module uses, so nothing is lost.
struct RetireEntry
{
    RetireKind kind = RetireKind::Buffer;
    // Object to destroy, packed with `PackHandle`.
    u64 object = 0;
    // Key of the fence the object is waiting on.
    u64 fence = 0;
    // Value the fence must reach before the object may be destroyed.
    u64 value = 0;
};

// A list of objects whose destruction waits for the GPU.
//
// A resource cannot be destroyed while a submission that reads it is still in
// flight. Destroying it immediately is safe in the null backend, because it
// finishes work inside `Submit`, and unsafe on every real one. The queue is
// therefore the caller's policy: it holds each object with the fence value the
// frame that last used it will signal, and releases the object once the timeline
// passes that value.
//
// The queue owns nothing but its entries. It never destroys on its own: a caller
// decides when to `Collect`, typically once per frame before recording.
class RetirementQueue
{
public:
    // Creates an empty queue that allocates its entry list from `allocator`.
    explicit RetirementQueue(memory::Allocator& allocator) noexcept;

    RetirementQueue(const RetirementQueue&) = delete;
    RetirementQueue& operator=(const RetirementQueue&) = delete;

    // Queues `buffer` for destruction once `fence` reaches `value`. Returns
    // `ErrorCode::InvalidArgument` for an invalid object handle or an invalid
    // fence handle, and `ErrorCode::Internal` when the entry list cannot grow.
    [[nodiscard]] Status<Error> Retire(BufferHandle buffer, FenceHandle fence, u64 value) noexcept;

    // Queues `texture`. Views over it are retired by the backend when it is
    // destroyed, exactly as `DestroyTexture` documents.
    [[nodiscard]] Status<Error> Retire(TextureHandle texture, FenceHandle fence,
                                       u64 value) noexcept;

    // Queues `view`.
    [[nodiscard]] Status<Error> Retire(TextureViewHandle view, FenceHandle fence,
                                       u64 value) noexcept;

    // Queues `sampler`.
    [[nodiscard]] Status<Error> Retire(SamplerHandle sampler, FenceHandle fence,
                                       u64 value) noexcept;

    // Queues `shader`. Pipelines created from it stay valid, so this is safe to
    // call before the pipelines that use it are retired.
    [[nodiscard]] Status<Error> Retire(ShaderHandle shader, FenceHandle fence, u64 value) noexcept;

    // Queues `pipeline`. The queue asks the device which kind it is when it
    // destroys it.
    [[nodiscard]] Status<Error> Retire(PipelineHandle pipeline, FenceHandle fence,
                                       u64 value) noexcept;

    // Queues `group`.
    [[nodiscard]] Status<Error> Retire(BindGroupHandle group, FenceHandle fence,
                                       u64 value) noexcept;

    // Queues `layout`. Groups created from it stay valid.
    [[nodiscard]] Status<Error> Retire(BindGroupLayoutHandle layout, FenceHandle fence,
                                       u64 value) noexcept;

    // Queues a pipeline layout. Pipelines created from it stay valid.
    [[nodiscard]] Status<Error> Retire(PipelineLayoutHandle layout, FenceHandle fence,
                                       u64 value) noexcept;

    // Queues a pipeline cache.
    [[nodiscard]] Status<Error> Retire(PipelineCacheHandle cache, FenceHandle fence,
                                       u64 value) noexcept;

    // Destroys every entry whose fence has reached its value and returns how
    // many objects were released. An entry whose destroy call reports a stale
    // handle is dropped without being counted, because a caller that destroyed
    // the object elsewhere must not be reported as a failure.
    [[nodiscard]] Result<u32, Error> Collect(Device& device) noexcept;

    // Destroys every entry now, ignoring fences, and returns how many were
    // released. Only safe after `Device::WaitIdle`.
    [[nodiscard]] u32 Flush(Device& device) noexcept;

    // Drops every entry without destroying anything. This is a teardown path for
    // a device that is already gone; the objects are leaked by definition and the
    // backend released them with the device.
    void Clear() noexcept;

    // Entries still waiting.
    [[nodiscard]] usize Pending() const noexcept;

    // True when nothing is waiting.
    [[nodiscard]] bool Empty() const noexcept;

    // Entries the queue can hold without growing.
    [[nodiscard]] usize Capacity() const noexcept;

private:
    [[nodiscard]] Status<Error> Enqueue(RetireKind kind, u64 packed, FenceHandle fence,
                                        u64 value) noexcept;

    memory::DynamicArray<RetireEntry> m_entries;
};

}  // namespace cyber::rhi
