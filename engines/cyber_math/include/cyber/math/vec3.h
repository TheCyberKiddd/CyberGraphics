#pragma once

#include <cyber/core/types.h>
#include <cyber/math/constants.h>

#include <cmath>

namespace cyber::math
{

struct Vec3
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    constexpr Vec3() noexcept = default;

    constexpr Vec3(f32 xValue, f32 yValue, f32 zValue) noexcept : x(xValue), y(yValue), z(zValue) {}

    explicit constexpr Vec3(f32 scalar) noexcept : x(scalar), y(scalar), z(scalar) {}

    [[nodiscard]] constexpr f32& operator[](usize index) noexcept
    {
        switch (index)
        {
        case 0:
            return x;
        case 1:
            return y;
        default:
            return z;
        }
    }

    [[nodiscard]] constexpr const f32& operator[](usize index) const noexcept
    {
        switch (index)
        {
        case 0:
            return x;
        case 1:
            return y;
        default:
            return z;
        }
    }

    [[nodiscard]] static constexpr Vec3 Zero() noexcept
    {
        return Vec3{};
    }

    [[nodiscard]] static constexpr Vec3 One() noexcept
    {
        return Vec3{1.0f, 1.0f, 1.0f};
    }

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& rhs) const noexcept
    {
        return Vec3{x + rhs.x, y + rhs.y, z + rhs.z};
    }

    [[nodiscard]] constexpr Vec3 operator-(const Vec3& rhs) const noexcept
    {
        return Vec3{x - rhs.x, y - rhs.y, z - rhs.z};
    }

    [[nodiscard]] constexpr Vec3 operator-() const noexcept
    {
        return Vec3{-x, -y, -z};
    }

    [[nodiscard]] constexpr Vec3 operator*(f32 scalar) const noexcept
    {
        return Vec3{x * scalar, y * scalar, z * scalar};
    }

    [[nodiscard]] constexpr Vec3 operator/(f32 scalar) const noexcept
    {
        return Vec3{x / scalar, y / scalar, z / scalar};
    }

    constexpr Vec3& operator+=(const Vec3& rhs) noexcept
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }

    constexpr Vec3& operator-=(const Vec3& rhs) noexcept
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }

    constexpr Vec3& operator*=(f32 scalar) noexcept
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    constexpr Vec3& operator/=(f32 scalar) noexcept
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }
};

[[nodiscard]] constexpr Vec3 operator*(f32 scalar, const Vec3& value) noexcept
{
    return value * scalar;
}

[[nodiscard]] constexpr f32 Dot(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] constexpr Vec3 Cross(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return Vec3{lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x};
}

[[nodiscard]] constexpr f32 LengthSquared(const Vec3& value) noexcept
{
    return Dot(value, value);
}

[[nodiscard]] inline f32 Length(const Vec3& value) noexcept
{
    return std::sqrt(LengthSquared(value));
}

[[nodiscard]] inline Vec3 Normalize(const Vec3& value) noexcept
{
    const f32 length = Length(value);
    return length > 0.0f ? value / length : Vec3::Zero();
}

[[nodiscard]] constexpr Vec3 Lerp(const Vec3& from, const Vec3& to, f32 factor) noexcept
{
    return from + (to - from) * factor;
}

[[nodiscard]] constexpr Vec3 Min(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return Vec3{Min(lhs.x, rhs.x), Min(lhs.y, rhs.y), Min(lhs.z, rhs.z)};
}

[[nodiscard]] constexpr Vec3 Max(const Vec3& lhs, const Vec3& rhs) noexcept
{
    return Vec3{Max(lhs.x, rhs.x), Max(lhs.y, rhs.y), Max(lhs.z, rhs.z)};
}

[[nodiscard]] constexpr Vec3 Abs(const Vec3& value) noexcept
{
    return Vec3{Abs(value.x), Abs(value.y), Abs(value.z)};
}

// `normal` must be normalized. The result points away from the surface.
[[nodiscard]] constexpr Vec3 Reflect(const Vec3& direction, const Vec3& normal) noexcept
{
    return direction - normal * (2.0f * Dot(direction, normal));
}

[[nodiscard]] inline bool ApproxEqual(const Vec3& lhs, const Vec3& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    return ApproxEqual(lhs.x, rhs.x, tolerance) && ApproxEqual(lhs.y, rhs.y, tolerance) &&
           ApproxEqual(lhs.z, rhs.z, tolerance);
}

static_assert(sizeof(Vec3) == sizeof(f32) * 3, "Vec3 must stay packed");
static_assert(alignof(Vec3) == alignof(f32), "Vec3 must have only float alignment");

}  // namespace cyber::math
