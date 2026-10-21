#pragma once

#include <cyber/math/mat4.h>
#include <cyber/math/quat.h>
#include <cyber/math/types.h>
#include <cyber/math/vec3.h>

namespace cyber::math
{

// p' = translation + rotation * (scale * p). Rotation is expected to be unit
// length. A TRS transform with non-uniform scale is an authoring record, not a
// closed algebra: composing two of them is not generally another TRS with a
// diagonal scale. Use ToMatrix() and Mat4 composition for hierarchies, and
// TryInverse(Mat4&) for the exact inverse.
struct Transform
{
    Vec3 translation{};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] static constexpr Transform Identity() noexcept
    {
        return Transform{};
    }

    [[nodiscard]] Mat4 ToMatrix() const noexcept
    {
        return Mat4::Translation(translation) * rotation.ToMat4() * Mat4::Scale(scale);
    }

    [[nodiscard]] Vec3 TransformPoint(Vec3 point) const noexcept
    {
        return translation +
               rotation.Rotate(Vec3{scale.x * point.x, scale.y * point.y, scale.z * point.z});
    }

    [[nodiscard]] Vec3 TransformVector(Vec3 vector) const noexcept
    {
        return rotation.Rotate(Vec3{scale.x * vector.x, scale.y * vector.y, scale.z * vector.z});
    }

    // Writes the exact inverse of ToMatrix() and returns false for a singular
    // transform. This is a 4x4 matrix because a non-uniform TRS inverse is not
    // generally expressible as another TRS with a diagonal scale.
    [[nodiscard]] bool TryInverse(Mat4& out) const noexcept;
};

[[nodiscard]] inline bool ApproxEqual(const Transform& lhs, const Transform& rhs,
                                      f32 tolerance = kEpsilon) noexcept
{
    return ApproxEqual(lhs.translation, rhs.translation, tolerance) &&
           ApproxEqual(lhs.rotation, rhs.rotation, tolerance) &&
           ApproxEqual(lhs.scale, rhs.scale, tolerance);
}

}  // namespace cyber::math
