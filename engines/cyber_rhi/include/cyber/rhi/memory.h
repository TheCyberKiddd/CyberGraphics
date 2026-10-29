#pragma once

#include <cyber/core/types.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;

// Statistics for one memory class of the GPU allocator (ADR 0018).
//
// The device-local, upload, and readback classes each hold a list of heaps the
// backend carved for resources; dedicated heaps go into the resource's class
// but are never sub-allocated. The numbers are policy, not accounting: nothing
// is freed to the OS until the device is destroyed, which is what makes these
// values stable enough to read.
struct MemoryHeapStats
{
    // Heaps the class owns, dedicated ones included.
    u32 blockCount = 0;
    // Sum of every heap's size.
    u64 reservedBytes = 0;
    // Sum of the sub-allocations the class currently serves.
    u64 usedBytes = 0;
    // Bytes of reserved space that are neither used nor free: alignment
    // padding above every sub-allocation. Fragmentation shows up as free space
    // rather than waste, which is deliberate.
    u64 wasteBytes = 0;
    // Largest stride the class could still sub-allocate without opening a new
    // heap, so a caller that needs one more 4 MiB texture knows whether the
    // answer is free or a new block.
    u64 largestFreeBytes = 0;
};

// The whole allocator, one heap's statistics per memory class. A zeroed struct
// is a valid answer for a device that does not own resources or an empty
// device; there is no separate validity flag.
struct MemoryStats
{
    MemoryHeapStats deviceLocal{};
    MemoryHeapStats upload{};
    MemoryHeapStats readback{};

    // Sum of the three classes, for one-line logging.
    [[nodiscard]] constexpr u64 ReservedBytes() const noexcept
    {
        return deviceLocal.reservedBytes + upload.reservedBytes + readback.reservedBytes;
    }

    [[nodiscard]] constexpr u64 UsedBytes() const noexcept
    {
        return deviceLocal.usedBytes + upload.usedBytes + readback.usedBytes;
    }
};

}  // namespace cyber::rhi
