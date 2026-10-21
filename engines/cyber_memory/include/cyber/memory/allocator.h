#pragma once

#include <cyber/memory/alignment.h>
#include <cyber/memory/types.h>

namespace cyber::memory
{

// Snapshot of an allocator's accounting. Capacity is the number of bytes the
// allocator can serve, used and peak are payload bytes, and allocations is the
// number of successful Allocate calls since the last Reset.
struct AllocatorStats
{
    usize capacity = 0;
    usize used = 0;
    usize peak = 0;
    usize allocations = 0;
};

// Explicit, non-throwing allocation interface. Alignment must be a non-zero
// power of two. Allocation returns nullptr on failure. Deallocate(nullptr) is a
// no-op. Bump allocators treat Deallocate as a no-op and reclaim through Reset
// or a stack marker.
//
// Implementations are not thread-safe. A caller that shares an allocator across
// threads must synchronize externally.
class Allocator
{
public:
    Allocator() = default;
    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;
    virtual ~Allocator() = default;

    // Returns a block of at least size bytes aligned to alignment, or nullptr.
    // A zero-size request returns nullptr.
    [[nodiscard]] virtual void* Allocate(usize size,
                                         usize alignment = kDefaultAlignment) noexcept = 0;

    // Releases a block previously returned by Allocate. Unknown or null pointers
    // are ignored by the allocator when it can validate them.
    virtual void Deallocate(void* pointer) noexcept = 0;

    // Reclaims all allocations. Pointers handed out before Reset are invalid.
    virtual void Reset() noexcept = 0;

    // Returns true when pointer lies inside this allocator's backing range.
    [[nodiscard]] virtual bool Owns(const void* pointer) const noexcept = 0;

    // Returns the current accounting snapshot.
    [[nodiscard]] virtual AllocatorStats Stats() const noexcept = 0;
};

}  // namespace cyber::memory
