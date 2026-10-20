#include <cyber/core/hash.h>

#include <bit>
#include <cstring>

namespace cyber::core
{
namespace
{

constexpr u64 kPrime1 = 11400714785074694791ULL;
constexpr u64 kPrime2 = 14029467366897019727ULL;
constexpr u64 kPrime3 = 1609587929392839161ULL;
constexpr u64 kPrime4 = 9650029242287828579ULL;
constexpr u64 kPrime5 = 2870177450012600261ULL;

constexpr u32 ByteSwap32(u32 value) noexcept
{
    return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) | ((value & 0xFF000000u) >> 24);
}

constexpr u64 ByteSwap64(u64 value) noexcept
{
    return ((value & 0x00000000000000FFull) << 56) | ((value & 0x000000000000FF00ull) << 40) |
           ((value & 0x0000000000FF0000ull) << 24) | ((value & 0x00000000FF000000ull) << 8) |
           ((value & 0x000000FF00000000ull) >> 8) | ((value & 0x0000FF0000000000ull) >> 24) |
           ((value & 0x00FF000000000000ull) >> 40) | ((value & 0xFF00000000000000ull) >> 56);
}

u32 Load32LE(const u8* data) noexcept
{
    u32 value = 0;
    std::memcpy(&value, data, sizeof(value));
    if constexpr (std::endian::native == std::endian::big)
    {
        value = ByteSwap32(value);
    }
    return value;
}

u64 Load64LE(const u8* data) noexcept
{
    u64 value = 0;
    std::memcpy(&value, data, sizeof(value));
    if constexpr (std::endian::native == std::endian::big)
    {
        value = ByteSwap64(value);
    }
    return value;
}

u64 Round(u64 accumulator, u64 input) noexcept
{
    accumulator += input * kPrime2;
    accumulator = std::rotl(accumulator, 31);
    accumulator *= kPrime1;
    return accumulator;
}

u64 MergeRound(u64 accumulator, u64 value) noexcept
{
    accumulator ^= Round(0, value);
    accumulator = accumulator * kPrime1 + kPrime4;
    return accumulator;
}

}  // namespace

u64 HashBytes(const void* data, usize size, u64 seed) noexcept
{
    if (data == nullptr && size != 0)
    {
        return 0;
    }

    static constexpr u8 kEmpty = 0;
    const auto* bytes = data != nullptr ? static_cast<const u8*>(data) : &kEmpty;
    const u8* cursor = bytes;
    const u8* const end = bytes + size;
    u64 hash = 0;

    if (size >= 32)
    {
        const u8* const limit = end - 32;
        u64 v1 = seed + kPrime1 + kPrime2;
        u64 v2 = seed + kPrime2;
        u64 v3 = seed;
        u64 v4 = seed - kPrime1;

        do
        {
            v1 = Round(v1, Load64LE(cursor));
            cursor += 8;
            v2 = Round(v2, Load64LE(cursor));
            cursor += 8;
            v3 = Round(v3, Load64LE(cursor));
            cursor += 8;
            v4 = Round(v4, Load64LE(cursor));
            cursor += 8;
        } while (cursor <= limit);

        hash = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
        hash = MergeRound(hash, v1);
        hash = MergeRound(hash, v2);
        hash = MergeRound(hash, v3);
        hash = MergeRound(hash, v4);
    }
    else
    {
        hash = seed + kPrime5;
    }

    hash += static_cast<u64>(size);

    while (cursor + 8 <= end)
    {
        const u64 k1 = Round(0, Load64LE(cursor));
        hash ^= k1;
        hash = std::rotl(hash, 27) * kPrime1 + kPrime4;
        cursor += 8;
    }

    if (cursor + 4 <= end)
    {
        hash ^= static_cast<u64>(Load32LE(cursor)) * kPrime1;
        hash = std::rotl(hash, 23) * kPrime2 + kPrime3;
        cursor += 4;
    }

    while (cursor < end)
    {
        hash ^= static_cast<u64>(*cursor) * kPrime5;
        hash = std::rotl(hash, 11) * kPrime1;
        ++cursor;
    }

    hash ^= hash >> 33;
    hash *= kPrime2;
    hash ^= hash >> 29;
    hash *= kPrime3;
    hash ^= hash >> 32;
    return hash;
}

u64 HashCombine(u64 seed, u64 value) noexcept
{
    seed ^= value + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

}  // namespace cyber::core
