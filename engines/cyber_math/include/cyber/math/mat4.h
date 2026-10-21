#pragma once

#include <cyber/math/constants.h>
#include <cyber/math/mat3.h>
#include <cyber/math/types.h>
#include <cyber/math/vec3.h>
#include <cyber/math/vec4.h>

namespace cyber::math
{

// Column-major 4x4 matrix: m[column][row]. The four columns are contiguous, so
// the object uploads directly to a column-major constant buffer.
struct alignas(16) Mat4
{
    Vec4 columns[4];

    constexpr Mat4() noexcept
        : columns{Vec4{1.0f, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 1.0f, 0.0f, 0.0f},
                  Vec4{0.0f, 0.0f, 1.0f, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, 1.0f}}
    {
    }

    constexpr Mat4(Vec4 column0, Vec4 column1, Vec4 column2, Vec4 column3) noexcept
        : columns{column0, column1, column2, column3}
    {
    }

    [[nodiscard]] constexpr Vec4& operator[](usize column) noexcept
    {
        return columns[column];
    }

    [[nodiscard]] constexpr const Vec4& operator[](usize column) const noexcept
    {
        return columns[column];
    }

    [[nodiscard]] static constexpr Mat4 Zero() noexcept
    {
        return Mat4{Vec4::Zero(), Vec4::Zero(), Vec4::Zero(), Vec4::Zero()};
    }

    [[nodiscard]] static constexpr Mat4 Identity() noexcept
    {
        return Mat4{};
    }

    [[nodiscard]] static constexpr Mat4 Translation(Vec3 translation) noexcept
    {
        return Mat4{Vec4{1.0f, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 1.0f, 0.0f, 0.0f},
                    Vec4{0.0f, 0.0f, 1.0f, 0.0f},
                    Vec4{translation.x, translation.y, translation.z, 1.0f}};
    }

    [[nodiscard]] static constexpr Mat4 Scale(Vec3 scale) noexcept
    {
        return Mat4{Vec4{scale.x, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, scale.y, 0.0f, 0.0f},
                    Vec4{0.0f, 0.0f, scale.z, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    }

    [[nodiscard]] static constexpr Mat4 Scale(f32 scale) noexcept
    {
        return Scale(Vec3{scale, scale, scale});
    }

    [[nodiscard]] static Mat4 RotationX(f32 radians) noexcept;
    [[nodiscard]] static Mat4 RotationY(f32 radians) noexcept;
    [[nodiscard]] static Mat4 RotationZ(f32 radians) noexcept;
    [[nodiscard]] static Mat4 RotationAxis(Vec3 axis, f32 radians) noexcept;

    // Right-handed view matrix. The camera looks from `eye` toward `target`,
    // down its local -Z axis. `up` must not be parallel to the view direction;
    // degenerate input returns identity.
    [[nodiscard]] static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept;

    // Projection constructors require 0 < nearPlane < farPlane, a finite
    // non-zero aspect, and 0 < fovYRadians < pi. Inputs are not clamped or
    // asserted in shipping builds; callers validate them at the API boundary.
    [[nodiscard]] static Mat4 PerspectiveReverseZ(f32 fovYRadians, f32 aspect, f32 nearPlane,
                                                  f32 farPlane) noexcept;
    [[nodiscard]] static Mat4 PerspectiveInfiniteReverseZ(f32 fovYRadians, f32 aspect,
                                                          f32 nearPlane) noexcept;
    [[nodiscard]] static Mat4 OrthographicReverseZ(f32 left, f32 right, f32 bottom, f32 top,
                                                   f32 nearPlane, f32 farPlane) noexcept;

    [[nodiscard]] Mat4 operator*(const Mat4& rhs) const noexcept
    {
        return Mat4{(*this) * rhs.columns[0], (*this) * rhs.columns[1], (*this) * rhs.columns[2],
                    (*this) * rhs.columns[3]};
    }

    [[nodiscard]] inline Vec4 operator*(const Vec4& rhs) const noexcept
    {
        return columns[0] * rhs.x + columns[1] * rhs.y + columns[2] * rhs.z + columns[3] * rhs.w;
    }

    // Transforms a homogeneous point and divides by w when the matrix is
    // projective. Use TransformVector for directions.
    [[nodiscard]] Vec3 TransformPoint(Vec3 point) const noexcept
    {
        Vec4 result = (*this) * Vec4{point.x, point.y, point.z, 1.0f};
        if (result.w != 0.0f && result.w != 1.0f)
        {
            result /= result.w;
        }
        return Vec3{result.x, result.y, result.z};
    }

    [[nodiscard]] Vec3 TransformVector(Vec3 vector) const noexcept
    {
        const Vec4 result = (*this) * Vec4{vector.x, vector.y, vector.z, 0.0f};
        return Vec3{result.x, result.y, result.z};
    }

    [[nodiscard]] constexpr Mat4 Transpose() const noexcept
    {
        return Mat4{Vec4{columns[0].x, columns[1].x, columns[2].x, columns[3].x},
                    Vec4{columns[0].y, columns[1].y, columns[2].y, columns[3].y},
                    Vec4{columns[0].z, columns[1].z, columns[2].z, columns[3].z},
                    Vec4{columns[0].w, columns[1].w, columns[2].w, columns[3].w}};
    }

    [[nodiscard]] f32 Determinant() const noexcept;

    // Returns false for a singular matrix and leaves `out` untouched.
    [[nodiscard]] bool TryInverse(Mat4& out) const noexcept;
};

[[nodiscard]] Mat3 UpperLeft3x3(const Mat4& matrix) noexcept;

[[nodiscard]] inline bool ApproxEqual(const Mat4& lhs, const Mat4& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    return ApproxEqual(lhs.columns[0], rhs.columns[0], tolerance) &&
           ApproxEqual(lhs.columns[1], rhs.columns[1], tolerance) &&
           ApproxEqual(lhs.columns[2], rhs.columns[2], tolerance) &&
           ApproxEqual(lhs.columns[3], rhs.columns[3], tolerance);
}

static_assert(sizeof(Mat4) == sizeof(f32) * 16, "Mat4 must have no padding");
static_assert(alignof(Mat4) == 16, "Mat4 must be 16-byte aligned");

}  // namespace cyber::math
