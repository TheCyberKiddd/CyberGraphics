#pragma once

#include <cyber/memory/linear_allocator.h>

namespace cyber::memory
{

// Per-frame linear allocator. BeginFrame reclaims everything allocated in the
// previous frame; EndFrame is an explicit no-op so frame boundaries are visible
// at call sites. Pointers from a previous frame are invalid after BeginFrame.
class FrameAllocator final : public LinearAllocator
{
public:
    using LinearAllocator::LinearAllocator;

    void BeginFrame() noexcept
    {
        Reset();
    }

    void EndFrame() noexcept {}
};

}  // namespace cyber::memory
