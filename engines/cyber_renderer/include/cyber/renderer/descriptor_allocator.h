#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/rhi/frame_ring.h>

#include <array>

namespace cyber::renderer
{

using cyber::core::u32;
using cyber::core::u64;

/// A persistent descriptor index protected against stale releases.
struct PersistentDescriptor
{
    /// False for an empty or stale handle.
    bool valid = false;
    /// Index into a fixed descriptor array.
    u32 index = 0;
    /// Generation advanced whenever this index is released.
    u32 generation = 0;
};

/// Fixed-capacity persistent descriptor index allocator.
class PersistentDescriptorAllocator
{
public:
    /// Construct an empty allocator.
    PersistentDescriptorAllocator() noexcept = default;
    /// Release allocator metadata.
    ~PersistentDescriptorAllocator();
    /// Allocators are move-only.
    PersistentDescriptorAllocator(const PersistentDescriptorAllocator&) = delete;
    /// Allocators are move-only.
    PersistentDescriptorAllocator& operator=(const PersistentDescriptorAllocator&) = delete;
    /// Transfer allocator metadata.
    PersistentDescriptorAllocator(PersistentDescriptorAllocator&& other) noexcept;
    /// Release this allocator and take another allocator's metadata.
    PersistentDescriptorAllocator& operator=(PersistentDescriptorAllocator&& other) noexcept;

    /// Initialize a fixed free list.
    ///
    /// @param capacity Number of persistent indices, non-zero.
    /// @param allocator Metadata allocator; borrowed until Destroy.
    /// @return Ok, or InvalidArgument/OutOfRange/Internal.
    [[nodiscard]] core::Status<core::Error> Initialize(u32 capacity,
                                                       memory::Allocator& allocator) noexcept;
    /// Allocate the lowest currently available logical index.
    /// @return A generation-counted descriptor, or OutOfRange when exhausted.
    [[nodiscard]] core::Result<PersistentDescriptor, core::Error> Allocate() noexcept;
    /// Release a live descriptor.
    /// @param descriptor Handle returned by Allocate.
    /// @return Ok, or InvalidArgument for a stale, malformed, or double-freed handle.
    [[nodiscard]] core::Status<core::Error> Free(PersistentDescriptor descriptor) noexcept;
    /// Test whether a handle still names an allocated index.
    /// @param descriptor Handle to test.
    /// @return True only for its currently allocated generation.
    [[nodiscard]] bool IsLive(PersistentDescriptor descriptor) const noexcept;
    /// Release all metadata and reset the allocator.
    void Destroy() noexcept;

private:
    void MoveFrom(PersistentDescriptorAllocator& other) noexcept;

    memory::Allocator* m_allocator = nullptr;
    u32* m_freeIndices = nullptr;
    u32* m_generations = nullptr;
    bool* m_allocated = nullptr;
    u32 m_capacity = 0;
    u32 m_freeCount = 0;
};

/// One contiguous transient descriptor allocation.
struct TransientDescriptorRange
{
    /// False when the allocation is invalid.
    bool valid = false;
    /// First index in the per-frame transient range.
    u32 first = 0;
    /// Number of descriptors reserved.
    u32 count = 0;
    /// Safe frame slot that owns this allocation.
    u32 frameSlot = 0;
    /// Frame index that owns this allocation.
    u64 frameIndex = 0;
};

/// Linear per-frame descriptor allocator reset only on a safe frame-slot reuse.
class TransientDescriptorAllocator
{
public:
    /// Construct an empty allocator.
    TransientDescriptorAllocator() noexcept = default;
    /// Release allocator metadata.
    ~TransientDescriptorAllocator();
    /// Allocators are move-only.
    TransientDescriptorAllocator(const TransientDescriptorAllocator&) = delete;
    /// Allocators are move-only.
    TransientDescriptorAllocator& operator=(const TransientDescriptorAllocator&) = delete;
    /// Transfer allocator metadata.
    TransientDescriptorAllocator(TransientDescriptorAllocator&& other) noexcept;
    /// Release this allocator and take another allocator's metadata.
    TransientDescriptorAllocator& operator=(TransientDescriptorAllocator&& other) noexcept;

    /// Initialize frame slots with one fixed transient range each.
    ///
    /// @param framesInFlight Slot count, in 1..rhi::kMaxFramesInFlight.
    /// @param capacityPerFrame Transient capacity of each slot, non-zero.
    /// @param allocator Metadata allocator; borrowed until Destroy.
    /// @return Ok, or InvalidArgument/OutOfRange/Internal.
    [[nodiscard]] core::Status<core::Error> Initialize(u32 framesInFlight, u32 capacityPerFrame,
                                                       memory::Allocator& allocator) noexcept;
    /// Reset a slot after the associated frame ring has made it safe to reuse.
    ///
    /// @param ring Frame ring that produced the token.
    /// @param frame Safe frame slot token.
    /// @return Ok, or InvalidArgument for a malformed or mismatched token.
    [[nodiscard]] core::Status<core::Error> BeginFrame(const rhi::FrameRing& ring,
                                                       const rhi::FrameSlot& frame) noexcept;
    /// Reserve a contiguous range in the active frame.
    /// @param count Non-zero descriptors to reserve.
    /// @return Range, or InvalidArgument/OutOfRange.
    [[nodiscard]] core::Result<TransientDescriptorRange, core::Error> Allocate(u32 count) noexcept;
    /// Release metadata and reset the allocator.
    void Destroy() noexcept;

private:
    struct Slot
    {
        u32 cursor = 0;
        u64 frameIndex = 0;
        bool hasFrame = false;
    };

    void MoveFrom(TransientDescriptorAllocator& other) noexcept;

    memory::Allocator* m_allocator = nullptr;
    Slot* m_slots = nullptr;
    u32 m_framesInFlight = 0;
    u32 m_capacityPerFrame = 0;
    u32 m_activeSlot = 0xFFFFFFFFU;
    u64 m_activeFrameIndex = 0;
};

}  // namespace cyber::renderer
