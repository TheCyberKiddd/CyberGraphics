#pragma once

#include <cyber/core/types.h>
#include <cyber/math/constants.h>

#include <cmath>

namespace cyber::math
{

struct Vec2
{
    f32 x = 0.0f;
    f32 y = 0.0f;

    constexpr Vec2() noexcept = default;

    constexpr Vec2(f32 xValue, f32 yValue) noexcept : x(xValue), y(yValue) {}

    explicit constexpr Vec2(f32 scalar) noexcept : x(scalar), y(scalar) {}

    [[nodiscard]] constexpr f32& operator[](usize index) noexcept
    {
        return index == 0 ? x : y;
    }

    [[nodiscard]] constexpr const f32& operator[](usize index) const noexcept
    {
        return index == 0 ? x : y;
    }

    [[nodiscard]] static constexpr Vec2 Zero() noexcept
    {
        return Vec2{};
    }

    [[nodiscard]] static constexpr Vec2 One() noexcept
    {
        return Vec2{1.0f, 1.0f};
    }

    [[nodiscard]] constexpr Vec2 operator+(const Vec2& rhs) const noexcept
    {
        return Vec2{x + rhs.x, y + rhs.y};
    }

    [[nodiscard]] constexpr Vec2 operator-(const Vec2& rhs) const noexcept
    {
        return Vec2{x - rhs.x, y - rhs.y};
    }

    [[nodiscard]] constexpr Vec2 operator-() const noexcept
    {
        return Vec2{-x, -y};
    }

    [[nodiscard]] constexpr Vec2 operator*(f32 scalar) const noexcept
    {
        return Vec2{x * scalar, y * scalar};
    }

    [[nodiscard]] constexpr Vec2 operator/(f32 scalar) const noexcept
    {
        return Vec2{x / scalar, y / scalar};
    }

    constexpr Vec2& operator+=(const Vec2& rhs) noexcept
    {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }

    constexpr Vec2& operator-=(const Vec2& rhs) noexcept
    {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }

    constexpr Vec2& operator*=(f32 scalar) noexcept
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    constexpr Vec2& operator/=(f32 scalar) noexcept
    {
        x /= scalar;
        y /= scalar;
        return *this;
    }
};

[[nodiscard]] constexpr Vec2 operator*(f32 scalar, const Vec2& value) noexcept
{
    return value * scalar;
}

[[nodiscard]] constexpr f32 Dot(const Vec2& lhs, const Vec2& rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

[[nodiscard]] constexpr f32 LengthSquared(const Vec2& value) noexcept
{
    return Dot(value, value);
}

[[nodiscard]] inline f32 Length(const Vec2& value) noexcept
{
    return std::sqrt(LengthSquared(value));
}

[[nodiscard]] inline Vec2 Normalize(const Vec2& value) noexcept
{
    const f32 length = Length(value);
    return length > 0.0f ? value / length : Vec2::Zero();
}

[[nodiscard]] constexpr Vec2 Lerp(const Vec2& from, const Vec2& to, f32 factor) noexcept
{
    return from + (to - from) * factor;
}

[[nodiscard]] constexpr Vec2 Min(const Vec2& lhs, const Vec2& rhs) noexcept
{
    return Vec2{Min(lhs.x, rhs.x), Min(lhs.y, rhs.y)};
}

[[nodiscard]] constexpr Vec2 Max(const Vec2& lhs, const Vec2& rhs) noexcept
{
    return Vec2{Max(lhs.x, rhs.x), Max(lhs.y, rhs.y)};
}

[[nodiscard]] constexpr Vec2 Abs(const Vec2& value) noexcept
{
    return Vec2{Abs(value.x), Abs(value.y)};
}

[[nodiscard]] inline bool ApproxEqual(const Vec2& lhs, const Vec2& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    return ApproxEqual(lhs.x, rhs.x, tolerance) && ApproxEqual(lhs.y, rhs.y, tolerance);
}

static_assert(sizeof(Vec2) == sizeof(f32) * 2, "Vec2 must stay packed");
static_assert(alignof(Vec2) == alignof(f32), "Vec2 must have only float alignment");

}  // namespace cyber::math
