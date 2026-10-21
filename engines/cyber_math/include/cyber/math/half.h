#pragma once

#include <cyber/math/types.h>

namespace cyber::math
{

// IEEE 754 binary16 storage value. Arithmetic is done in f32. NaN payloads are
// canonicalized on conversion; the sign bit is preserved.
class Half
{
public:
    constexpr Half() noexcept = default;

    [[nodiscard]] static constexpr Half FromBits(u16 bits) noexcept
    {
        Half result;
        result.m_bits = bits;
        return result;
    }

    [[nodiscard]] static Half FromFloat(f32 value) noexcept;

    [[nodiscard]] f32 ToFloat() const noexcept;

    [[nodiscard]] constexpr u16 Bits() const noexcept
    {
        return m_bits;
    }

    [[nodiscard]] constexpr bool IsFinite() const noexcept
    {
        return (m_bits & 0x7C00u) != 0x7C00u;
    }

    [[nodiscard]] constexpr bool IsInf() const noexcept
    {
        return (m_bits & 0x7FFFu) == 0x7C00u;
    }

    [[nodiscard]] constexpr bool IsNaN() const noexcept
    {
        return (m_bits & 0x7C00u) == 0x7C00u && (m_bits & 0x03FFu) != 0;
    }

    [[nodiscard]] constexpr bool IsNegative() const noexcept
    {
        return (m_bits & 0x8000u) != 0;
    }

    [[nodiscard]] friend constexpr bool operator==(Half lhs, Half rhs) noexcept
    {
        return lhs.m_bits == rhs.m_bits;
    }

    [[nodiscard]] friend constexpr bool operator!=(Half lhs, Half rhs) noexcept
    {
        return lhs.m_bits != rhs.m_bits;
    }

private:
    u16 m_bits = 0;
};

static_assert(sizeof(Half) == 2, "Half must be exactly two bytes");

}  // namespace cyber::math
