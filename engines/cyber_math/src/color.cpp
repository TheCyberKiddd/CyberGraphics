#include <cyber/math/color.h>

#include <cmath>

namespace cyber::math
{

f32 SrgbToLinear(f32 value) noexcept
{
    const f32 encoded = Saturate(value);
    if (encoded <= 0.04045f)
    {
        return encoded / 12.92f;
    }
    return static_cast<f32>(std::pow((static_cast<f64>(encoded) + 0.055) / 1.055, 2.4));
}

f32 LinearToSrgb(f32 value) noexcept
{
    const f32 linear = Saturate(value);
    if (linear <= 0.0031308f)
    {
        return linear * 12.92f;
    }
    const f32 encoded =
        static_cast<f32>(1.055 * std::pow(static_cast<f64>(linear), 1.0 / 2.4) - 0.055);
    return Saturate(encoded);
}

Color FromSrgb(const Color& color) noexcept
{
    return Color{SrgbToLinear(color.r), SrgbToLinear(color.g), SrgbToLinear(color.b), color.a};
}

Color ToSrgb(const Color& color) noexcept
{
    return Color{LinearToSrgb(color.r), LinearToSrgb(color.g), LinearToSrgb(color.b), color.a};
}

}  // namespace cyber::math
