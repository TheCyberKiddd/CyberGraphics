#pragma once

#include <cyber/math/constants.h>
#include <cyber/math/mat3.h>
#include <cyber/math/mat4.h>
#include <cyber/math/types.h>
#include <cyber/math/vec3.h>

#include <cmath>

namespace cyber::math
{

// Unit rotation quaternion in (x, y, z, w) order. Multiplication is Hamilton:
// q1 * q2 applies q2 first. Rotate() assumes the quaternion is normalized.
struct Quat
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 1.0f;

    constexpr Quat() noexcept = default;

    constexpr Quat(f32 xValue, f32 yValue, f32 zValue, f32 wValue) noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }

    [[nodiscard]] static constexpr Quat Identity() noexcept
    {
        return Quat{};
    }

    [[nodiscard]] static Quat FromAxisAngle(Vec3 axis, f32 radians) noexcept
    {
        const Vec3 normalizedAxis = cyber::math::Normalize(axis);
        if (cyber::math::LengthSquared(normalizedAxis) == 0.0f)
        {
            return Identity();
        }
        const f32 halfAngle = radians * 0.5f;
        const f32 sine = std::sin(halfAngle);
        return Quat{normalizedAxis.x * sine, normalizedAxis.y * sine, normalizedAxis.z * sine,
                    std::cos(halfAngle)};
    }

    // YXZ Euler input: q = qY * qX * qZ. This applies roll, then pitch, then
    // yaw. Euler extraction is deliberately not provided.
    [[nodiscard]] static Quat FromEuler(f32 yawRadians, f32 pitchRadians, f32 rollRadians) noexcept;

    [[nodiscard]] static Quat FromMat3(const Mat3& matrix) noexcept;
    [[nodiscard]] static Quat FromMat4(const Mat4& matrix) noexcept;

    // Shortest-arc rotation that takes `from` to `to`. Zero vectors return
    // identity; opposite vectors choose a stable perpendicular axis.
    [[nodiscard]] static Quat FromTo(Vec3 from, Vec3 to) noexcept;

    [[nodiscard]] static Quat Nlerp(const Quat& from, const Quat& to, f32 factor) noexcept;
    [[nodiscard]] static Quat Slerp(const Quat& from, const Quat& to, f32 factor) noexcept;

    [[nodiscard]] constexpr Quat operator*(const Quat& rhs) const noexcept
    {
        return Quat{w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
                    w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
                    w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
                    w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z};
    }

    [[nodiscard]] constexpr Quat Conjugate() const noexcept
    {
        return Quat{-x, -y, -z, w};
    }

    [[nodiscard]] constexpr f32 Dot(const Quat& rhs) const noexcept
    {
        return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w;
    }

    [[nodiscard]] constexpr f32 LengthSquared() const noexcept
    {
        return Dot(*this);
    }

    [[nodiscard]] inline f32 Length() const noexcept
    {
        return std::sqrt(LengthSquared());
    }

    [[nodiscard]] inline Quat Normalize() const noexcept
    {
        const f32 length = Length();
        return length > 0.0f ? Quat{x / length, y / length, z / length, w / length} : Identity();
    }

    // Convenience for unit quaternions. For non-unit input, normalize first or
    // use Conjugate() / LengthSquared() explicitly.
    [[nodiscard]] Vec3 Rotate(Vec3 vector) const noexcept
    {
        const Vec3 q{x, y, z};
        const Vec3 twiceCross = Cross(q, vector) * 2.0f;
        return vector + twiceCross * w + Cross(q, twiceCross);
    }

    [[nodiscard]] Mat3 ToMat3() const noexcept
    {
        const f32 xx = x * x;
        const f32 yy = y * y;
        const f32 zz = z * z;
        const f32 xy = x * y;
        const f32 xz = x * z;
        const f32 yz = y * z;
        const f32 wx = w * x;
        const f32 wy = w * y;
        const f32 wz = w * z;

        return Mat3{Vec3{1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy)},
                    Vec3{2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx)},
                    Vec3{2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy)}};
    }

    [[nodiscard]] Mat4 ToMat4() const noexcept
    {
        const Mat3 rotation = ToMat3();
        return Mat4{Vec4{rotation[0].x, rotation[0].y, rotation[0].z, 0.0f},
                    Vec4{rotation[1].x, rotation[1].y, rotation[1].z, 0.0f},
                    Vec4{rotation[2].x, rotation[2].y, rotation[2].z, 0.0f},
                    Vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    }
};

[[nodiscard]] inline bool ApproxEqual(const Quat& lhs, const Quat& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    return ApproxEqual(lhs.x, rhs.x, tolerance) && ApproxEqual(lhs.y, rhs.y, tolerance) &&
           ApproxEqual(lhs.z, rhs.z, tolerance) && ApproxEqual(lhs.w, rhs.w, tolerance);
}

static_assert(sizeof(Quat) == sizeof(f32) * 4, "Quat must have no padding");

}  // namespace cyber::math
