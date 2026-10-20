#pragma once

#include <cyber/core/types.h>

#include <span>
#include <string_view>

namespace cyber::core
{

// Stable, non-cryptographic xxHash64. Do not use for signatures, passwords, or
// collision-resistant storage. The seed is part of the hash and should be fixed
// per use case.
[[nodiscard]] u64 HashBytes(const void* data, usize size, u64 seed = 0) noexcept;

[[nodiscard]] inline u64 HashBytes(std::span<const Byte> data, u64 seed = 0) noexcept
{
    return HashBytes(data.data(), data.size(), seed);
}

[[nodiscard]] inline u64 HashString(std::string_view text, u64 seed = 0) noexcept
{
    return HashBytes(text.data(), text.size(), seed);
}

// Mixes an existing hash with a value. The result is deterministic across
// platforms and compiler versions.
[[nodiscard]] u64 HashCombine(u64 seed, u64 value) noexcept;

}  // namespace cyber::core
