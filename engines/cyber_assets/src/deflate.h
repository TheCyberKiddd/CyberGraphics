#pragma once

#include <cyber/assets/asset.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>

namespace cyber::assets::detail
{

using cyber::core::Byte;
using cyber::core::u32;
using cyber::core::usize;
using cyber::memory::DynamicArray;
using cyber::memory::Span;

/// Inflates a zlib stream (RFC 1950 wrapping RFC 1951 DEFLATE). Every block,
/// code, and length is validated. `maxOutput` is a hard cap; the decoder fails
/// instead of growing past it. The Adler-32 trailer is verified.
[[nodiscard]] ImportStatus InflateZlib(Span<const Byte> input, DynamicArray<Byte>& output,
                                       usize maxOutput, ImportError& error) noexcept;

/// Reflectless CRC-32 (IEEE 802.3) used by PNG chunks.
[[nodiscard]] u32 Crc32(u32 seed, const Byte* data, usize size) noexcept;

/// Reads a big-endian 16-bit value from a byte buffer.
[[nodiscard]] inline u16 ReadBigEndian16(const Byte* data) noexcept
{
    return static_cast<u16>((static_cast<u32>(std::to_integer<u8>(data[0])) << 8u) |
                            static_cast<u32>(std::to_integer<u8>(data[1])));
}

/// Reads a big-endian 24-bit value from a byte buffer.
[[nodiscard]] inline u32 ReadBigEndian24(const Byte* data) noexcept
{
    return (static_cast<u32>(std::to_integer<u8>(data[0])) << 16u) |
           (static_cast<u32>(std::to_integer<u8>(data[1])) << 8u) |
           static_cast<u32>(std::to_integer<u8>(data[2]));
}

/// Reads a big-endian 32-bit value from a byte buffer.
[[nodiscard]] inline u32 ReadBigEndian32(const Byte* data) noexcept
{
    return (static_cast<u32>(std::to_integer<u8>(data[0])) << 24u) |
           (static_cast<u32>(std::to_integer<u8>(data[1])) << 16u) |
           (static_cast<u32>(std::to_integer<u8>(data[2])) << 8u) |
           static_cast<u32>(std::to_integer<u8>(data[3]));
}

}  // namespace cyber::assets::detail
