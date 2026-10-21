#pragma once

#include <cyber/math/constants.h>
#include <cyber/math/types.h>
#include <cyber/math/vec3.h>

namespace cyber::math
{

// Column-major 3x3 matrix: m[column][row].
struct Mat3
{
    Vec3 columns[3];

    constexpr Mat3() noexcept
        : columns{Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}}
    {
    }

    constexpr Mat3(Vec3 column0, Vec3 column1, Vec3 column2) noexcept
        : columns{column0, column1, column2}
    {
    }

    [[nodiscard]] constexpr Vec3& operator[](usize column) noexcept
    {
        return columns[column];
    }

    [[nodiscard]] constexpr const Vec3& operator[](usize column) const noexcept
    {
        return columns[column];
    }

    [[nodiscard]] static constexpr Mat3 Zero() noexcept
    {
        return Mat3{Vec3::Zero(), Vec3::Zero(), Vec3::Zero()};
    }

    [[nodiscard]] static constexpr Mat3 Identity() noexcept
    {
        return Mat3{};
    }

    [[nodiscard]] constexpr Mat3 operator*(const Mat3& rhs) const noexcept
    {
        return Mat3{(*this) * rhs.columns[0], (*this) * rhs.columns[1], (*this) * rhs.columns[2]};
    }

    [[nodiscard]] constexpr Vec3 operator*(const Vec3& rhs) const noexcept
    {
        return columns[0] * rhs.x + columns[1] * rhs.y + columns[2] * rhs.z;
    }

    [[nodiscard]] constexpr Mat3 Transpose() const noexcept
    {
        return Mat3{Vec3{columns[0].x, columns[1].x, columns[2].x},
                    Vec3{columns[0].y, columns[1].y, columns[2].y},
                    Vec3{columns[0].z, columns[1].z, columns[2].z}};
    }

    [[nodiscard]] constexpr f32 Determinant() const noexcept
    {
        return Dot(columns[0], Cross(columns[1], columns[2]));
    }

    // Returns false for a singular matrix and leaves `out` untouched.
    [[nodiscard]] bool TryInverse(Mat3& out) const noexcept;
};

[[nodiscard]] inline bool ApproxEqual(const Mat3& lhs, const Mat3& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    return ApproxEqual(lhs.columns[0], rhs.columns[0], tolerance) &&
           ApproxEqual(lhs.columns[1], rhs.columns[1], tolerance) &&
           ApproxEqual(lhs.columns[2], rhs.columns[2], tolerance);
}

static_assert(sizeof(Mat3) == sizeof(f32) * 9, "Mat3 must have no padding");
static_assert(alignof(Mat3) == alignof(f32), "Mat3 must stay tightly packed");

}  // namespace cyber::math
