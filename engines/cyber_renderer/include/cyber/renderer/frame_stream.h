#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/frame_ring.h>
#include <cyber/rhi/resource_state.h>

#include <array>

namespace cyber::renderer
{

using cyber::core::u32;
using cyber::core::u64;

/// Configuration for a fence-safe per-frame upload stream.
struct FrameBufferStreamDesc
{
    /// Number of resources slots, matching the associated FrameRing.
    u32 framesInFlight = rhi::kDefaultFramesInFlight;
    /// Capacity of each GPU destination and staging buffer, in bytes.
    u64 capacityBytes = 0;
    /// Maximum allocations recorded in one frame slot before uploads are recorded.
    u32 maxAllocationsPerFrame = 0;
    /// Buffer binding semantics used by consumers.
    rhi::BindingType bindingType = rhi::BindingType::UniformBuffer;
    /// Structured element stride for storage streams; zero selects a raw buffer.
    u32 elementStride = 0;
};

/// One allocation in a frame stream.
struct FrameBufferAllocation
{
    /// False when no valid allocation is present.
    bool valid = false;
    /// GPU-readable destination buffer.
    rhi::BufferHandle buffer{};
    /// Aligned byte offset inside the destination buffer.
    u64 offsetBytes = 0;
    /// Number of caller bytes copied into the stream.
    u64 sizeBytes = 0;
    /// Frame index that owns this allocation.
    u64 frameIndex = 0;
};

/// Fence-safe, aligned upload buffers for per-frame renderer data.
///
/// The caller obtains slots from `rhi::FrameRing::BeginFrame`, records uploads
/// and consumers in queue order, and submits the ring's signal value. This
/// object does not wait, submit, or own the frame ring.
class FrameBufferStream
{
public:
    /// Construct an empty stream.
    FrameBufferStream() noexcept = default;
    /// Release stream buffers. The device and allocator must still be alive.
    ~FrameBufferStream();
    /// Streams are move-only.
    FrameBufferStream(const FrameBufferStream&) = delete;
    /// Streams are move-only.
    FrameBufferStream& operator=(const FrameBufferStream&) = delete;
    /// Transfer the stream's resources.
    FrameBufferStream(FrameBufferStream&& other) noexcept;
    /// Release this stream and transfer another stream's resources.
    FrameBufferStream& operator=(FrameBufferStream&& other) noexcept;

    /// Create staging and GPU destination buffers for each frame slot.
    ///
    /// @param device Live RHI device; borrowed for the stream lifetime.
    /// @param allocator Allocator for bounded upload-range metadata; borrowed.
    /// @param desc Stream shape and binding type.
    /// @return The stream, or InvalidArgument/OutOfRange/Unsupported/Internal.
    [[nodiscard]] static core::Result<FrameBufferStream, core::Error>
    Create(rhi::Device& device, memory::Allocator& allocator,
           const FrameBufferStreamDesc& desc) noexcept;

    /// Begin writing into a slot that the frame ring has made safe to reuse.
    ///
    /// @param ring Ring that produced the frame token.
    /// @param frame Slot returned by that ring's successful BeginFrame call.
    /// @return Ok, or InvalidArgument for a stale, malformed, or mismatched token;
    ///         InvalidArgument if unrecorded allocations would be discarded.
    [[nodiscard]] core::Status<core::Error> BeginFrame(const rhi::FrameRing& ring,
                                                       const rhi::FrameSlot& frame) noexcept;

    /// Copy bytes into the active slot and reserve an aligned GPU offset.
    ///
    /// @param bytes Non-empty source bytes, copied before this call returns.
    /// @return Allocation, or InvalidArgument/OutOfRange/Internal on failure.
    [[nodiscard]] core::Result<FrameBufferAllocation, core::Error>
    Allocate(memory::Span<const memory::Byte> bytes) noexcept;

    /// Record staging copies and the destination transition for the active slot.
    ///
    /// @param commandList Recording list on the caller's ordered upload path.
    /// @return Ok when no upload is pending or when all copies and barriers record.
    ///         On partial failure, discard the command list; pending ranges remain.
    [[nodiscard]] core::Status<core::Error> RecordUploads(rhi::CommandList& commandList) noexcept;

    /// Return the buffer usage selected at creation.
    /// @return Uniform or storage usage.
    [[nodiscard]] rhi::BindingType BindingType() const noexcept;
    /// Return stream capacity per frame slot.
    /// @return Capacity in bytes, or zero for an empty stream.
    [[nodiscard]] u64 CapacityBytes() const noexcept;
    /// Return whether the stream owns live RHI resources.
    /// @return True after successful creation and before destruction.
    [[nodiscard]] bool IsValid() const noexcept;
    /// Return the destination buffer for a frame slot.
    /// @param slot Frame-slot index below the configured frames-in-flight count.
    /// @return Live destination handle, or an invalid handle for an out-of-range slot.
    [[nodiscard]] rhi::BufferHandle DestinationBuffer(u32 slot) const noexcept;

    /// Destroy all owned RHI buffers and release metadata.
    ///
    /// @return Ok, or the first resource-destruction error. The stream is reset
    ///         regardless; callers must ensure resources are no longer in flight.
    [[nodiscard]] core::Status<core::Error> Destroy() noexcept;

private:
    struct Slot
    {
        rhi::BufferHandle staging{};
        rhi::BufferHandle destination{};
        rhi::ResourceState destinationState = rhi::ResourceState::Undefined;
        u64 cursor = 0;
        u64 frameIndex = 0;
        u32 rangeCount = 0;
        bool hasFrame = false;
        bool stagingReady = false;
    };

    struct UploadRange
    {
        u64 offsetBytes = 0;
        u64 sizeBytes = 0;
    };

    void MoveFrom(FrameBufferStream& other) noexcept;
    void Reset() noexcept;

    rhi::Device* m_device = nullptr;
    memory::Allocator* m_allocator = nullptr;
    UploadRange* m_ranges = nullptr;
    u64 m_capacityBytes = 0;
    u32 m_maxAllocationsPerFrame = 0;
    u32 m_framesInFlight = 0;
    u32 m_activeSlot = 0xFFFFFFFFU;
    u64 m_activeFrameIndex = 0;
    bool m_poisoned = false;
    rhi::BindingType m_bindingType = rhi::BindingType::Count;
    rhi::ResourceState m_readState = rhi::ResourceState::Undefined;
    std::array<Slot, rhi::kMaxFramesInFlight> m_slots{};
};

}  // namespace cyber::renderer
