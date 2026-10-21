#pragma once

#include <cyber/memory/types.h>

namespace cyber::memory
{

inline constexpr u32 kInvalidHandleIndex = 0xFFFFFFFFU;

// Generation-counted handle. Index identifies a slot and generation identifies a
// particular lifetime of that slot. A default handle is invalid.
template <typename Tag>
struct Handle
{
    u32 index = kInvalidHandleIndex;
    u32 generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index != kInvalidHandleIndex;
    }

    constexpr void Reset() noexcept
    {
        index = kInvalidHandleIndex;
        generation = 0;
    }

    friend constexpr bool operator==(Handle lhs, Handle rhs) noexcept
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    friend constexpr bool operator!=(Handle lhs, Handle rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

}  // namespace cyber::memory
