#pragma once

#include <cyber/memory/types.h>

#include <cstddef>

namespace cyber::memory
{

// The largest alignment the tracking and free-list allocators can promise.
inline constexpr usize kMaxTrackedAlignment = 64;

// The alignment every root allocation is guaranteed to satisfy.
inline constexpr usize kDefaultAlignment = alignof(std::max_align_t);

// Returns true when value is a non-zero power of two.
[[nodiscard]] constexpr bool IsPowerOfTwo(usize value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

// Rounds value up to the next multiple of alignment. alignment must be a
// non-zero power of two. The caller is responsible for checking overflow.
[[nodiscard]] constexpr usize AlignUp(usize value, usize alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// Returns true when value is a multiple of alignment.
[[nodiscard]] constexpr bool IsAligned(usize value, usize alignment) noexcept
{
    return (value & (alignment - 1)) == 0;
}

// Rounds a pointer forward to alignment. alignment must be a power of two.
[[nodiscard]] inline void* AlignPointer(void* pointer, usize alignment) noexcept
{
    const auto address = reinterpret_cast<usize>(pointer);
    return reinterpret_cast<void*>(AlignUp(address, alignment));
}

// Rounds a constant pointer forward to alignment.
[[nodiscard]] inline const void* AlignPointer(const void* pointer, usize alignment) noexcept
{
    const auto address = reinterpret_cast<usize>(pointer);
    return reinterpret_cast<const void*>(AlignUp(address, alignment));
}

}  // namespace cyber::memory
