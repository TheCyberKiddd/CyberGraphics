#pragma once

#include <cyber/math/types.h>

#include <cmath>

namespace cyber::math
{

inline constexpr f32 kPi = 3.14159265358979323846f;
inline constexpr f32 kTwoPi = 6.28318530717958647692f;
inline constexpr f32 kHalfPi = 1.57079632679489661923f;
inline constexpr f32 kQuarterPi = 0.78539816339744830962f;
inline constexpr f32 kEpsilon = 1.0e-6f;

inline constexpr f32 kDegreesToRadians = kPi / 180.0f;
inline constexpr f32 kRadiansToDegrees = 180.0f / kPi;

[[nodiscard]] constexpr f32 Min(f32 lhs, f32 rhs) noexcept
{
    return lhs < rhs ? lhs : rhs;
}

[[nodiscard]] constexpr f32 Max(f32 lhs, f32 rhs) noexcept
{
    return lhs > rhs ? lhs : rhs;
}

[[nodiscard]] constexpr f32 Clamp(f32 value, f32 low, f32 high) noexcept
{
    return value < low ? low : (value > high ? high : value);
}

[[nodiscard]] constexpr f32 Saturate(f32 value) noexcept
{
    return Clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] constexpr f32 Lerp(f32 from, f32 to, f32 factor) noexcept
{
    return from + (to - from) * factor;
}

[[nodiscard]] constexpr f32 Abs(f32 value) noexcept
{
    return value < 0.0f ? -value : value;
}

[[nodiscard]] constexpr f32 Sign(f32 value) noexcept
{
    return value < 0.0f ? -1.0f : (value > 0.0f ? 1.0f : 0.0f);
}

[[nodiscard]] constexpr f32 Radians(f32 degrees) noexcept
{
    return degrees * kDegreesToRadians;
}

[[nodiscard]] constexpr f32 Degrees(f32 radians) noexcept
{
    return radians * kRadiansToDegrees;
}

[[nodiscard]] inline bool ApproxEqual(f32 lhs, f32 rhs, f32 tolerance = kEpsilon) noexcept
{
    if (tolerance < 0.0f)
    {
        return false;
    }
    if (lhs == rhs)
    {
        return true;
    }
    return std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] inline bool IsFinite(f32 value) noexcept
{
    return std::isfinite(value);
}

}  // namespace cyber::math
