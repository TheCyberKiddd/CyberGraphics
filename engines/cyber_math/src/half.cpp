#include <cyber/math/half.h>

#include <bit>
#include <cmath>
#include <limits>

namespace cyber::math
{
namespace
{

constexpr u16 kSignMask = 0x8000u;
constexpr u16 kExponentMask = 0x7C00u;
constexpr u16 kMantissaMask = 0x03FFu;
constexpr u16 kInfinity = 0x7C00u;
constexpr u32 kFloatSignMask = 0x80000000u;
constexpr u32 kFloatMantissaMask = 0x007FFFFFu;

}  // namespace

Half Half::FromFloat(f32 value) noexcept
{
    const u32 bits = std::bit_cast<u32>(value);
    const u16 sign = static_cast<u16>((bits >> 16) & kSignMask);
    const u32 exponent = (bits >> 23) & 0xFFu;
    const u32 mantissa = bits & kFloatMantissaMask;

    if (exponent == 0xFFu)
    {
        if (mantissa == 0)
        {
            return FromBits(static_cast<u16>(sign | kInfinity));
        }
        u16 payload = static_cast<u16>(mantissa >> 13);
        if (payload == 0)
        {
            payload = 1;
        }
        // Keep the result quiet and non-zero.
        payload = static_cast<u16>(payload | 0x0200u);
        return FromBits(static_cast<u16>(sign | kInfinity | payload));
    }

    if ((bits & 0x7FFFFFFFu) == 0)
    {
        return FromBits(sign);
    }

    const f64 magnitude = std::fabs(static_cast<f64>(value));
    if (magnitude >= 65520.0)
    {
        return FromBits(static_cast<u16>(sign | kInfinity));
    }
    if (magnitude < 0x1p-25)
    {
        return FromBits(sign);
    }

    if (magnitude < 0x1p-14)
    {
        const f64 scaled = std::ldexp(magnitude, 24);
        u32 halfMantissa = static_cast<u32>(std::floor(scaled));
        const f64 fraction = scaled - static_cast<f64>(halfMantissa);
        if (fraction > 0.5 || (fraction == 0.5 && (halfMantissa & 1u) != 0))
        {
            ++halfMantissa;
        }
        return FromBits(static_cast<u16>(sign | static_cast<u16>(halfMantissa)));
    }

    int binaryExponent = 0;
    const f64 significand = std::frexp(magnitude, &binaryExponent);
    const f64 scaledMantissa = (significand * 2.0 - 1.0) * 1024.0;
    u32 halfMantissa = static_cast<u32>(std::floor(scaledMantissa));
    const f64 fraction = scaledMantissa - static_cast<f64>(halfMantissa);
    if (fraction > 0.5 || (fraction == 0.5 && (halfMantissa & 1u) != 0))
    {
        ++halfMantissa;
    }

    u32 halfExponent = static_cast<u32>(binaryExponent - 1 + 15);
    if (halfMantissa == 1024)
    {
        halfMantissa = 0;
        ++halfExponent;
    }
    if (halfExponent >= 31)
    {
        return FromBits(static_cast<u16>(sign | kInfinity));
    }

    return FromBits(static_cast<u16>(sign | (halfExponent << 10) | halfMantissa));
}

f32 Half::ToFloat() const noexcept
{
    const u16 exponent = static_cast<u16>((m_bits & kExponentMask) >> 10);
    const u16 mantissa = static_cast<u16>(m_bits & kMantissaMask);
    const bool negative = (m_bits & kSignMask) != 0;

    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            return negative ? -0.0f : 0.0f;
        }
        const f32 magnitude = static_cast<f32>(std::ldexp(static_cast<f64>(mantissa), -24));
        return negative ? -magnitude : magnitude;
    }

    if (exponent == 31)
    {
        if (mantissa == 0)
        {
            return negative ? -std::numeric_limits<f32>::infinity()
                            : std::numeric_limits<f32>::infinity();
        }
        const u32 nanBits = 0x7FC00000u | (negative ? kFloatSignMask : 0u);
        return std::bit_cast<f32>(nanBits);
    }

    const f32 magnitude =
        static_cast<f32>(std::ldexp(static_cast<f64>(1024 + mantissa), exponent - 25));
    return negative ? -magnitude : magnitude;
}

}  // namespace cyber::math
