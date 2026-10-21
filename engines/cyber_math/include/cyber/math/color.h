#pragma once

#include <cyber/math/constants.h>
#include <cyber/math/types.h>

namespace cyber::math
{

// Linear RGBA color. Alpha is linear opacity and is not encoded to sRGB.
struct Color
{
    f32 r = 0.0f;
    f32 g = 0.0f;
    f32 b = 0.0f;
    f32 a = 1.0f;

    constexpr Color() noexcept = default;

    constexpr Color(f32 red, f32 green, f32 blue, f32 alpha = 1.0f) noexcept
        : r(red), g(green), b(blue), a(alpha)
    {
    }
};

// IEC 61966-2-1 transfer functions. RGB input is clamped to [0, 1]; alpha is
// never converted.
[[nodiscard]] f32 SrgbToLinear(f32 value) noexcept;
[[nodiscard]] f32 LinearToSrgb(f32 value) noexcept;

[[nodiscard]] Color FromSrgb(const Color& color) noexcept;
[[nodiscard]] Color ToSrgb(const Color& color) noexcept;

[[nodiscard]] inline bool ApproxEqual(const Color& lhs, const Color& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    return ApproxEqual(lhs.r, rhs.r, tolerance) && ApproxEqual(lhs.g, rhs.g, tolerance) &&
           ApproxEqual(lhs.b, rhs.b, tolerance) && ApproxEqual(lhs.a, rhs.a, tolerance);
}

}  // namespace cyber::math
