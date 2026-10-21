#pragma once

#include <cyber/memory/linear_allocator.h>

namespace cyber::memory
{

// Linear allocator with explicit LIFO markers. FreeTo only accepts a marker at
// or below the current allocation point. Freeing out of order is rejected.
class StackAllocator final : public LinearAllocator
{
public:
    using Marker = usize;

    using LinearAllocator::LinearAllocator;

    [[nodiscard]] Marker Mark() const noexcept
    {
        return Used();
    }

    [[nodiscard]] bool FreeTo(Marker marker) noexcept
    {
        return TryResetTo(marker);
    }
};

}  // namespace cyber::memory
